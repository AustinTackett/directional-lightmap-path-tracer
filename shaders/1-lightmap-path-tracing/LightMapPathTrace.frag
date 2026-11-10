#version 430 core

// Sorry for the text dumps, but I needed to work through the math and document it or else I would forget.

/*
    Constants
*/
#define BIND_BVH_NODES 0
#define BIND_BVH_FACE_INDICES 1
#define BIND_VERTICES 2u
#define BIND_FACES 3u
#define BIND_LIGHT 0
#define UNIT_HDRI 6u
#define ATTACHMENT_LIGHT_MAP 0u

#define EPS 1e-8f
#define INF 1e30
#define NEG_INF (-1e30)
#define PI 3.14159265359
#define MAX_STACK_DEPTH 128u
#define UINT_MAX 0xFFFFFFFFu

#define MAX_SAMPLES = 32;
#define MAX_BOUNCES = 5;

/*
    Vertex/Face data
*/
struct Face
{
    uint vertexIndex0;
    uint vertexIndex1;
    uint vertexIndex2;
}

layout(std430, binding = BIND_FACES) readonly buffer Faces
{
    Face faces[];
};

struct Vertex 
{
    vec3 position;
    vec3 normal;
    vec2 materialTexCoords; // ([0, 1], [0, 1])
    vec2 lightMapTexCoords; // ([0, 1], [0, 1])
};

layout(std430, binding = BIND_VERTICES) readonly buffer Vertices
{
    Vertex vertices[];
};

/*
    Bvh
*/
struct Aabb
{
    vec3 min;
    vec3 max;
};

struct BvhNode
{
    Aabb aabb;

    uint faceOffset;
    uint faceCount;

    int leftChildIndex;
    int rightChildIndex;
};

layout(std430, binding = BIND_BVH_NODES) readonly buffer BvhNodes
{
    BvhNode nodes[];
};

layout(std430, binding = BIND_BVH_FACE_INDICES) readonly buffer BvhFaceIndices
{
    uint bvhFaceIndices[];
};

/*
    Ray traversal/intersections
*/

struct Ray
{
    vec3  origin;
    vec3  direction;
};

struct RayHit
{
    vec3 position;
    vec3 normal;
    bool t;
};

/*
    Moeller–Trumbore

                     M                                               T
        [ -D.x   (B.x-A.x)   (C.x-A.x) ] [ t ]   [ O.x - A.x ]    [ T.x ]
        [ -D.y   (B.y-A.y)   (C.y-A.y) ] [ u ] = [ O.y - A.y ] =  [ T.y ]
        [ -D.z   (B.z-A.z)   (C.z-A.z) ] [ v ]   [ O.z - A.z ]    [ T.z ]

              B                Where t is the distance from the ray origin the the intersection occurs
           u / \    D*t        at and u and v are the the distance along edges AB and AC the intersection
            / <-\--------- O   occurs at.
           A — — C              
               v               T is vector from the rays origin to one of the triangles' vertices
*/
bool rayIntersectFace(Ray ray, Face face, out RayHit rayHit) 
{
    Vertex vertA = vertices[face.vertexIndex0];
    Vertex vertB = vertices[face.vertexIndex1];
    Vertex vertC = vertices[face.vertexIndex2];

    vec3 T = vec3(ray.origin - vertA.position);
    mat3 M = mat3(-ray.direction, vertB.position - vertA.position, vertC.position - vertA.position);
    
    // Check if ray is parrallel to plane
    float detM = determinant(M);

    // Cramers rule says no uniqne solution exists
    if (detM < EPS) {
        return false;
    }

    // Using Cramers rule to find unique solution
    mat3 Mx = mat3(T, M[1], M[2]); 
    mat3 My = mat3(M[0], T, M[2]); 
    mat3 Mz = mat3(M[0], M[1], T);

    float t = determinant(Mx)/detM;
    float u = determinant(My)/detM;
    float v = determinant(Mz)/detM;

    // Check for miss (u and v indicate its outside of triangle face)
    if (u < 0.0f || v < 0.0f || u + v > 1.0f)
        return false;
    // check if triangle was behind ray (t is a negative distance)
    if (t <= EPS)
        return false;

    // barycentric interpolation of position and normal
    vec3 baryCoords = vec3(1.0 - u - v, u, v);
    rayHit.position =
        baryCoords.x * vertA.materialTexCoords +
        baryCoords.y * vertB.materialTexCoords +
        baryCoords.z * vertC.materialTexCoords;

    rayHit.normal = normalize(
        baryCoords.x * vertA.normal +
        baryCoords.y * vertB.normal +
        baryCoords.z * vertC.normal
    );
    rayHit.t = t;
    return true;
};

/*
    Slab Method

    ____max      for i = [x, y, z]
    |     |         ray enter: min(min.i - o.i / d.i, max.i - o.i / d.i)
    |     |         ray exit:  min(min.i - o.i / d.i, max.i - o.i / d.i)
    min___|
*/
bool rayIntersectAabb(Ray ray, Aabb aabb)
{
    // if direction.i == 0 then results will be either +/- INF (on most hardware...)
    vec3 invDirection = 1 / ray.direction;

    // compute when/if ray intersects aabb
    vec3 t_xyz_low  = (aabb.min - ray.origin) * invDirection;
    vec3 t_xyz_high = (aabb.max - ray.origin) * invDirection;
    
    vec3 t_xyz_enter = min(t_xyz_low, t_xyz_high);
    vec3 t_xyz_exit  = max(t_xyz_low, t_xyz_high);
    float t_enter = max(max(t_xyz_enter.x, t_xyz_enter.y), t_xyz_enter.z);
    float t_exit  = min(min(t_xyz_exit.x, t_xyz_exit.y), t_xyz_exit.z);

    bool isMiss   = t_enter > t_exit;
    bool isBehind = t_exit < 0.0f;
    isHit = !isMiss && !isBehind;
    return isHit;
};

bool rayTraverseBvh(Ray ray, uint bvhRootNodeIndex, out RayHit rayHit)
{
    BvhNode nodeStack[MAX_STACK_DEPTH];
    uint    stackPtr = 0;

    // Root Intersection
    BvhNode rootNode = bvhNodes[bvhRootNodeIndex];
    bool hitRoot = rayIntersectAabb(ray, rootNode.aabb);
    if (!hitRoot) { 
         return false;
    }

    // Find face with closest intersection
    rayHit.t = INF;
    nodeStack[stackPtr++] = rootNode;
    while (stackPtr > 0) {
        BvhNode node = nodeStack[--stackPtr];

        bool isHit  = rayIntersectAabb(ray, node.aabb);
        bool isLeaf = node.leftChildIndex < 0 && node.rightChildIndex < 0;

        // check if face intersection is new closest intersection
        if (isLeaf && isHit) {
            bool   hitFace = false;
            RayHit candidateRayHit(vec3(0), vec3(0), INF);
            for(uint i = 0; i < node.faceCount; i++) {
                uint faceIndex = bvhFaceIndices[node.faceOffset + i];
                hitFace = rayIntersectFace(ray, faces[faceIndex], candidateRayHit);
                rayHit = hitFace && candidateRayHit.t < rayHit.t ? candidateRayHit : rayHit;  
            }
        }

        // Add child nodes to stack
        bool hasNoStackOverflow = stackPtr+2 <= MAX_STACK_DEPTH;
        if (!isLeaf && isHit && hasNoStackOverflow) {
            nodeStack[stackPtr++] = bvhNodes[node.leftChildIndex];
            nodeStack[stackPtr++] = bvhNodes[node.rightChildIndex];
        }
    }
    return rayHit.t < INF ? true : false;
};

/*
    Uniform sampling of 2D values with Halton Sequences
*/
struct HaltonSeqState
{
    uint index1;
    uint base1;

    uint index2;
    uint base2;
}

void initHaltonSequence(inout HaltonSeqState state, uint initIndex1, uint initIndex2)
{
    state.index1 = 0u;
    state.base1  = 3u;
    state.index2 = 0u;
    state.base2  = 2u;
}

// pseudo random deterministic sequence of uniformly distributed 2d values in ( [0, 1), [0, 1) )
vec2 stepHaltonSequence2f(inout HaltonSeqState state) {
    uint  i1 = state.index1++;
    uint  i2 = state.index2++;
    float f1 = 1;
    float f2 = 1;
    float r1 = 0;
    float r2 = 0;
    
    while (i1 > 0u)
    {
        f1 = f1 / float(state.base1);
        r1 = r1 + f1 * float(i1 % state.base1);
        i1 = i1 / state.base1; // truncates
    }
    while (i2 > 0u)
    {
        f2 = f2 / float(state.base2);
        r2 = r2 + f2 * float(i2 % state.base2);
        i2 = i2 / state.base2; // truncates
    }

    return clamp(vec2(r1, r2), 0.0, 1.0);
}

// Uniform float from [0, 1)
float stepHaltonSequence1f(inout HaltonSeqState state) {
    uint  i1 = state.index1++;
    float f1 = 1;
    float r1 = 0;
    
    while (i1 > 0u)
    {
        f1 = f1 / float(state.base1);
        r1 = r1 + f1 * float(i1 % state.base1);
        i1 = i1 / state.base1; // truncates
    }

    return clamp(r1, 0.0, 1.0);
}

// Returns uniform uint in range: [minValue, maxValue]
uint stepHaltonSequence1u(inout HaltonSeqState state, uint minValue, uint maxValue)
{
    uint  count = maxValue - minValue + 1u;
    float value = stepHaltonSeqState1f(state);
    uint offset = min(uint(value * float(count)), count - 1u);

    return minValue + offset;
}


/*
    Cosine Weighted Hemisphere Sampling
*/

/*
    Pdf of cosine weighted hemisphere sampling

    Given a direction from a half hemisphere oriented to the normal of the surface 
    output the probability of choosing that direction. 

    Given a sampling function "sample(w)" that increases in regions of high sample density,
    we need to normalize it by finding "?" to have a valid pdf such that:
        -> integral_+Omega[p(w)]dOmega = 1. 
        -> where integral_+Omega[p(w)]dOmega = integral_0_2pi integral_0_pi/2 [p(w)] sin(theta) dphi dtheta
        -> and w := (theta, phi)
    
    Given p(w) = sample(w) * ?, then the integral_+Omega[sample(w) * ?]dOmega = 1.

    Cosine Weighted Sampling: sample density closer to 45deg, sample(w) = cos(w_theta)
    integral_+Omega[cos(theta) * ?]dOmega = 1 -> ? = 1 / pi 
    p(w) = cos(theta) / pi

        n    w   w := direction
        | t /    n := normal       p(theta, phi) = cos(theta) / pi
        |  /     t := theta        
        | /      
      ———————
*/
float pdfCosineWeighted(float theta)
{
    return clamp(cos(theta), 0.0, 1.0) / PI;
}

/*
    Converting samples from uniform pdf to samples from the cosine weighted pdf 

    Use inverse cdf of cosine weighted pdf to convert a uniformly sampled theta and phi into a sample 
    from the cosine weighted pdf.

    pdf_cosine(theta, phi) = cos(theta) / pi

    Joint cdf evaulated for some theta_0 and phi_0 value (we don't actually use this)
    (probability some theta and phi is less than theta_0 and phi_0 using the scalar valued function cdf: R^2 -> [0, 1])
    -> cdf_cosine(theta_0, phi_0) = integral_0_(phi_0) integral_0_(theta_0) [cos(theta) / pi] sin(theta) dtheta dphi 
     = cdf_cosine(theta_0, phi_0) = ( phi * sin^2(theta) ) / (2pi)

    Marginal cdf's evaulated for some theta_0 and phi_0 value (cdf_marginal: R -> [0, 1])
    -> cdf_cosine_theta(theta_0) =  integral_0_2pi integral_0_(theta_0) [cos(theta) / pi] sin(theta) dtheta dphi
    = sin^2(theta_0)
    -> cdf_cosine_phi(phi_0) =  integral_0_(phi_0) integral_0_pi/2 [cos(theta) / pi] sin(theta) dtheta dphi
    = phi_0 / 2pi

    Marginal cdf inverses
    -> cdf_cosine_theta^-1(cdf_cosine_theta(theta_0)) = theta_0
    = cdf_cosine_theta^-1( sin^2(theta_0) ) = theta_0
    -> given that arcsin(sqrt(x)) is the inverse of sin^2(x)
    -> cdf_cosine_theta^-1(x) = arcsin(sqrt(x))
    
    -> cdf_cosine_phi^-1(cdf_cosine_phi(phi_0)) = phi_0
    = cdf_cosine_phi^-1(phi_0 / 2pi) = phi_0
    -> cdf_cosine_phi^-1(x) = 2pi * x

    Given a uniformly sampled values (u1, u2) contained in [0, 1] we can convert it to the probability from the pdf by using the inverse cdf
    (since theta and phi samples are indepedent we can use the marginal cdf for each variable)
    -> cdf_cosin_theta^-1(u1) = theta
    = arcsin(sqrt(u1))        = theta
    
    -> cdf_cosin_phi^-1(u2) = phi
    = 2pi * u2              = phi
*/
vec2 sampleCosineWeightedAngle(inout HaltonSeqState state)
{
    // use 2d uniform distribution with values in ( [0, 1), [0, 1) )
    vec2  uniform2d = stepHaltonSequence2f(state);

    // convert to spherical angles
    float theta = asin(sqrt(clamp(uniform2d.x, 0.0, 1.0)));
    float phi   = 2 * PI *  uniform2d.y;
    return vec2(theta, phi);
}

/*
    Get new direction oriented with respect to normal of surface

      (side view)                           (top down)
        N    D      T := tangent                 D
        | t /       t := theta                  /        p := phi
        |  /        N := normal                /  p       
   T____| /         D := direction      T-----N
*/
vec3 sampleDirection(vec3 normal, out float probability, inout HaltonSeqState state)
{
    // sample phi and theta from pdf (tangent space)
    vec2  thetaPhi = sampleCosineWeightedAngle(state);
    float theta = thetaPhi.x;
    float phi   = thetaPhi.y;

    probability = pdfCosineWeighted(theta);

    // get new direction in cartesian coordinates (tangent space)
    vec3 direction = normalize(vec3(
        sin(theta)*cos(phi), 
        sin(theta)*sin(phi), 
        cos(theta)
    ));

    // Get transform from tangent space to world space
    normal = normalize(normal);
    
    bool isParralleltoYaxis = abs(dot(normal, vec3(0, 1, 0))) > 1-EPS;
    vec3 tangent = normalize( 
        isParralleltoYaxis 
        ? cross(normal, vec3(0, 0, 1)) 
        : cross(normal, vec3(0, 1, 0))
    ); // abitrary perpendicular vector from the cross product of a non-parrallel vector
    
    vec3 bitangent = cross(normal, tangent);

    // transform new direction into world space
    mat3 TBN = mat3(tangent, bitangent, normal);
    return TBN * direction;
};

vec3 sampleDiffuseBrdf(vec3 albedo, vec3 incomingDir, vec3 outgoingDir, vec3 normal, float importancePdf, out float throughput, out float irradiance)
{

};

/*
    Environment Lighting
*/

/*
    Hdri's

    Transforming 3D directions to spherically mapped equirectangular uvs

    Cartesion to Spherical (y-up coordinate system)
    r     = sqrt(x^2 + y^2 +z^2)   
    theta = arctan(z / x)
    phi   = arcos(y / sqrt(x^2 + y^2 +z^2))
    
    Bounds of angles in spherical coordinates
    u = [-pi, pi] -> range of 2*pi
    v = [0, pi]   -> range of pi
    
    Normalize by range get values from [-1, 1]
    u = (theta / 2*pi) + 0.5  -> (add 0.5 to u to go from [-0.5, 0.5] to [-1, 1])
    v = phi / pi              -> [-1, 1]
    (The OpenGL standard flips v for texture samplers so v' = 1-v)
*/
vec2 getHdriTexCoords(vec3 direction)
{
    vec3 d = normalize(direction);
    d = clamp(d, -1.0, 1.0);       // just in case floating point error

    float r     = length(d);
    float theta = atan(d.z, d.x);
    float phi   = acos(d.y);       // r = 1 given d is normalized

    float u = (theta / (2*PI)) + 0.5;
    float v = 1 - phi / PI;        // flipped
    return vec2(u, v);
}

/*
    Uniforms
*/
layout (binding = UNIT_HDRI) uniform sampler2D hdri;
uniform uint screenWidth;
uniform uint screenHeight;

// lights that doen't have 3d extent (only hitable by NEE)
struct SingularityLight
{
    uint lightType; // 0 := directional, 1 := point
    vec3 position;
    vec3 direction;
    vec3 irradiance;
};

uniform uint lightCount;
layout(std140, binding = BIND_LIGHT) uniform Lights
{
    SingularityLight singularityLights[1];
};

/*
    Input
*/
in vec3 fragPosition;
in vec3 fragNormal;

/*
    Output
*/
layout (location = ATTACHMENT_LIGHT_MAP) out vec3 fragOutputIrradiance

void main() 
{
    uvec2 fragCoord = uvec2(gl_FragCoord.xy);
    uint  fragIndex = fragCoord.y * screenWidth + fragCoord.x;

    HaltonSeqState haltonState;
    initHaltonSequence(haltonState, fragIndex, 2*fragIndex);
    
    vec3 irradiance   = vec3(0, 0, 0);
    vec3 normal       = fragNormal;
    vec3 position     = fragPosition; 
    float probability = 1;
    for(int i = 0; i < MAX_SAMPLES; i++) {
        vec3 sampleDir = sampleDirection(normal, probability, haltonState);
        Ray ray(position, sampleDir);
        
        // update throughput

        for(int bounce = 0; bounce < MAX_BOUNCES; bounce++) {
            RayHit rayHit;
            bool   isHit = rayTraverseBvh(ray, 0, rayHit);
            
            if (!isHit) {
                vec4 envRadiance = texture(hdri, getHdriTexCoords(ray.sampleDir));
                break;
            }

            // next event estimation
            randomLightIndex = stepHaltonSeq1u(haltonState) % lightCount;
            SingularityLight randomLight = singularityLights[randomLightIndex];
            Ray neeRay = Ray(position, )

            // sample brdf 

            // compute throughput

            // compute termination probability
            // either choose to terminate or 
            // update throughput and continue: throughput / (1-termination_probability)

            // spawn new ray
        }
    }

    fragOutputIrradiance = irradiance / MAX_SAMPLES;
}
