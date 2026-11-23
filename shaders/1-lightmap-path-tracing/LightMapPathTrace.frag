#version 430 core
#extension GL_ARB_bindless_texture : require // bindless textures
#extension GL_NV_gpu_shader5 : require       // non-uniform bindless texture access
layout(early_fragment_tests) in;

/*
    Constants
*/
#define BIND_SSBO_BVH_NODES 0u
#define BIND_SSBO_BVH_FACE_INDICES 1u
#define BIND_SSBO_VERTICES 2u
#define BIND_SSBO_FACES 3u
#define BIND_SSBO_MATERIALS 4u
#define BIND_SSBO_TEXTURE_HANDLES 5u
#define BIND_SSBO_FACE_DATA 6u
#define BIND_SSBO_PIXEL_STATE 7u
#define BIND_SSBO_L1_SH 8u

#define BIND_UBO_LIGHT 0u

#define TEX_UNIT_HDRI 6u
#define TEX_UNIT_NOISE 7u

#define FRAME_ATTACHMENT_LIGHT_MAP 0u

#define BVH_ROOT_NODE 0u

#define LIGHT_MAX_COUNT 4u

#define EPS 1e-8f     // general near-zero value
#define EPS_RAY 1e-5f // larger near-zero value to avoid ray-triangle self-intersections
#define INF 1e30
#define PI 3.14159265359
#define MAX_STACK_DEPTH 48u

#define MAX_SAMPLE_LUMINANCE 10.0

/*
    Uniforms
*/
layout (binding = TEX_UNIT_HDRI)  uniform sampler2D hdri;

layout (binding = TEX_UNIT_NOISE) uniform sampler2D noiseTexture;
uniform bool  hasHdri;
uniform uint  screenWidth;
uniform uint  lightCount;
uniform uint  maxBounces;
uniform uint  minSamples;
uniform float relativeError;
uniform float absoluteError;


// std430 array stride: 32 bytes, matching PixelState in main.cpp.
struct PixelState
{
    vec3  mean;
    float luminanceMean;
    float M2;
    uint  count;
};

layout(std430, binding = BIND_SSBO_PIXEL_STATE) buffer PixelStates
{
    PixelState pixelStates[];
};

bool hasConverged(PixelState state)
{
    if (state.count < max(minSamples, 2u)) return false;
    float variance = max(state.M2 / float(state.count - 1u), 0.0);

    // Approximate 95% confidence interval
    float confidenceInterval = 1.96 * sqrt(variance / float(state.count));
    return confidenceInterval <= max(absoluteError, relativeError * abs(state.luminanceMean));
}

/*
    Vertex Data (VBO data)
*/
struct Vertex 
{
    vec3 position;
    vec3 normal;
    vec2 materialTexCoords; // ([0, 1], [0, 1])
    vec2 lightMapTexCoords; // ([0, 1], [0, 1])
};

layout(std430, binding = BIND_SSBO_VERTICES) readonly buffer Vertices
{
    Vertex vertices[];
};

/*
    Face Vertex Indices (EBO data)
*/
struct Face
{
    uint vertexIndex0;
    uint vertexIndex1;
    uint vertexIndex2;
};

layout(std430, binding = BIND_SSBO_FACES) readonly buffer Faces
{
    Face faces[];
};

/*
    Per Face Data
*/
struct FaceMaterial
{
    uint materialIndex;
};

layout(std430, binding = BIND_SSBO_FACE_DATA) readonly buffer FaceMaterials
{
    FaceMaterial faceMaterials[];
};

/*
    Material Definition
    -> -1 texture index := missing texture
*/
struct Material
{
    vec4  baseColor;
    vec3  emissive;
    float metallic;
    float roughness;
    float normal;
    float occlusion;
    int   baseColorTextureIndex;
    int   normalTextureIndex;
    int   emissiveTextureIndex;
    int   metallicRoughnessTextureIndex;
    int   occlusionTextureIndex;
    int   alphaMode;
    float alphaCutoff;
};

layout(std430, binding = BIND_SSBO_MATERIALS) readonly buffer Materials
{
    Material materials[];
};

/*
    Texture Sampling
    -> returns linear color values
*/
layout(std430, binding = BIND_SSBO_TEXTURE_HANDLES) readonly buffer TextureHandles
{
    uvec2 textureHandles[];
};

// returns linear color
vec3 sampleAlbedo(Material material, vec2 uv)
{
    vec3 albedo = material.baseColor.rgb;
    if (material.baseColorTextureIndex >= 0) {
        uint index = uint(material.baseColorTextureIndex);
        albedo *= textureLod(sampler2D(textureHandles[index]), uv, 0.0).rgb;
    }
    return albedo;
}

// returns radiance
vec3 sampleEmission(Material material, vec2 uv)
{
    vec3 emission = material.emissive;
    if (material.emissiveTextureIndex >= 0) {
        uint index = uint(material.emissiveTextureIndex);
        emission *= textureLod(sampler2D(textureHandles[index]), uv, 0.0).rgb;
    }
    return emission;
}

/*
    Image Based Lighting
    -> Hdri stores radiance given direction in world space
*/
vec2 getHdriTexCoords(vec3 direction)
{
    // cartesian to spherical coords normalized to ([0, 1], [0, 1])
    vec3 d = normalize(direction);
    d = clamp(d, -1.0, 1.0);       // just in case floating point error

    float theta = atan(d.z, d.x);
    float phi   = acos(d.y);       // r = 1 given d is normalized

    float u = (theta / (2*PI)) + 0.5;
    float v = phi / PI;
    return vec2(u, v);
}

vec3 sampleHdri(vec2 uv)
{
    return hasHdri ? textureLod(hdri, uv, 0.0).rgb : vec3(0.0);
}

/*
    Uniform Pseudo-Random Numbers
*/
struct HaltonSeqState
{
    vec2 rotation;
    uint index1;
    uint base1;

    uint index2;
    uint base2;
};

void initHaltonSequence(inout HaltonSeqState state, uint initIndex1, uint initIndex2)
{
    state.index1 = initIndex1;
    state.base1  = 3u;
    state.index2 = initIndex2;
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
        i1 = i1 / state.base1; 
    }
    while (i2 > 0u)
    {
        f2 = f2 / float(state.base2);
        r2 = r2 + f2 * float(i2 % state.base2);
        i2 = i2 / state.base2; 
    }

    return fract(vec2(r1, r2) + state.rotation);
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
        i1 = i1 / state.base1; 
    }

    return fract(r1 + state.rotation.x);
}

/*
    Hemisphere Sampling
*/vec3 sampleHemisphereDirection(inout HaltonSeqState state, vec3 normal, out float pdf)
{
    // probability to do uniform or cosine weighted sample
    const float uniformProbability = 0.25; 

    vec2 u = stepHaltonSequence2f(state);

    float cosTheta;
    if (u.x < uniformProbability)
    {
        // Uniform hemisphere
        float xi = u.x / uniformProbability;
        cosTheta = xi;
    }
    else
    {
        // Cosine-weighted hemisphere
        float xi = (u.x - uniformProbability) / (1.0 - uniformProbability);
        cosTheta = sqrt(1.0 - xi);
    }

    // Get direction in space of normal of hemisphere
    float sinTheta = sqrt(max(
        0.0,
        1.0 - cosTheta * cosTheta
    ));
    float phi = 2.0 * PI * u.y;
    vec3 localDirection = vec3(
        sinTheta * cos(phi),
        sinTheta * sin(phi),
        cosTheta
    );    

    // Get map from local normal space to world space
    normal = normalize(normal);
    vec3 tangent = normalize(
        abs(normal.y) > 0.999
        ? cross(normal, vec3(0, 0, 1))
        : cross(normal, vec3(0, 1, 0))
    );
    vec3 bitangent = cross(normal, tangent);

    // Map local hemisphere direction to world space
    vec3 direction = normalize(
        tangent   * localDirection.x +
        bitangent * localDirection.y +
        normal    * localDirection.z
    );

    // Compute probability of taking that direciton
    float cosine = max(dot(normal, direction), 0.0);
    pdf = uniformProbability * (1.0 / (2.0 * PI)) +
        (1.0 - uniformProbability) * (cosine / PI);

    return direction;
}

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

layout(std430, binding = BIND_SSBO_BVH_NODES) readonly buffer BvhNodes
{
    BvhNode nodes[];
};

layout(std430, binding = BIND_SSBO_BVH_FACE_INDICES) readonly buffer BvhFaceIndices
{
    uint bvhFaceIndices[];
};

/*
    Brdf's
*/
vec3 diffuseBrdf(vec3 albedo)
{
    // Lambertian
    return albedo / PI;
}

/*
    Triangle-Face Ray Intersections
*/
struct Ray
{
    vec3  origin;
    vec3  direction;
};

struct FaceRayHit
{
    uint  faceIndex;
    float u;
    float v;
    float t;
};

bool rayIntersectFace(Ray ray, uint faceIndex, out FaceRayHit faceHit)
{    
    //  [ -D.x   (B.x-A.x)   (C.x-A.x) ] [ t ]   [ O.x - A.x ]    [ T.x ]
    //  [ -D.y   (B.y-A.y)   (C.y-A.y) ] [ u ] = [ O.y - A.y ] =  [ T.y ]
    //  [ -D.z   (B.z-A.z)   (C.z-A.z) ] [ v ]   [ O.z - A.z ]    [ T.z ]

    // t = |Mt|/|M|, u = |Mu|/|M|, v = |Mv|/|M|

    // |Mt| := |T, M[1], M[2]| where M[i] and T are column vectors
    // |Mu| := |M[0], T, M[2]| where M[i] and T are column vectors
    // |Mv| := |M[0], M[1], T| where M[i] and T are column vectors
    // -> |M|  = dot(M[:, 0], cross(M[:, 1], M[:, 2])) where M[:, i] are rows in M
    // -> |Mu| = dot(M[:, 0], cross(T, M[:, 2])) where M[:, i] are rows in M
    
    // P := cross(D, AC)
    // Q := cross(T, AB)
    // |M| = dot(AB, P)
    // |Mu| = dot(T, P)
    // |Mv| = dot(D, Q)
    // |Mt| = dot(AC, Q)

    // u = dot(T, P) / |M|
    // v = dot(D, Q) / |M|
    // t = dot(AC, Q) / |M|

    faceHit.faceIndex = faceIndex;
    Face face = faces[faceIndex];
    Vertex vertA = vertices[face.vertexIndex0];
    Vertex vertB = vertices[face.vertexIndex1];
    Vertex vertC = vertices[face.vertexIndex2];

    vec3 T  = ray.origin - vertA.position;
    vec3 AB = vertB.position - vertA.position;
    vec3 AC = vertC.position - vertA.position;
    
    vec3 P = cross(ray.direction, AC);
    float detM = dot(AB, P);

    // Cramers rule says no unique solution exists
    if (abs(detM) < EPS) {
        return false;
    }
    
    // check hit is inside edge defined by coord u
    float invDetM = 1.0 / detM;
    faceHit.u = dot(T, P) * invDetM;
    if (faceHit.u < 0.0 || faceHit.u > 1.0)
        return false;

    // check hit is inside edge defined by coord v
    vec3 Q = cross(T, AB);
    faceHit.v = dot(ray.direction, Q) * invDetM;
    if (faceHit.v < 0.0 || faceHit.u + faceHit.v > 1.0)
        return false;

    // get distance it occured
    faceHit.t = dot(AC, Q) * invDetM;

    // ignore intersection opposite to the ray direction
    if (faceHit.t <= EPS_RAY)
        return false;

    faceHit.faceIndex = faceIndex;

    return true;
}

/*
    AAB Ray Intersection
*/
struct AabbRayHit
{
    float tMin;
    float tMax;
};

bool rayIntersectAabb(Ray ray, Aabb aabb, float maxT, out AabbRayHit rayHit) 
{
    rayHit.tMin = 0.0;
    rayHit.tMax = maxT; // max t for early termination

    // compute intersection for each axis (x, y, z)
    for (int axis = 0; axis < 3; ++axis) {
        float origin    = ray.origin[axis];
        float direction = ray.direction[axis];

        // check if origin is in side line segment if direction is parrallel
        if (direction == 0.0) {
            if (origin < aabb.min[axis] || origin > aabb.max[axis]) {
                return false;
            }
            continue;
        }

        // find t of entry and exit of aabb
        float invDirection = 1.0 / direction;
        float t0 = (aabb.min[axis] - origin) * invDirection;
        float t1 = (aabb.max[axis] - origin) * invDirection;

        rayHit.tMin = max(rayHit.tMin, min(t0, t1));
        rayHit.tMax = min(rayHit.tMax, max(t0, t1));

        if (rayHit.tMin > rayHit.tMax)
            return false;
    }

    return true;
}

bool traceFaceRay(Ray ray, uint bvhRootNodeIndex, out FaceRayHit rayHit, bool anyHit, uint ignoredFaceIndex)
{
    uint  nodeStack[MAX_STACK_DEPTH];
    float distanceStack[MAX_STACK_DEPTH];
    uint  stackPtr = 0u;

    // Find closest intersection
    rayHit.t = INF;

    // Test root AABB
    AabbRayHit rootHit;
    if (!rayIntersectAabb(ray, nodes[bvhRootNodeIndex].aabb, rayHit.t, rootHit)) {
        return false;
    }

    nodeStack[stackPtr]     = bvhRootNodeIndex;
    distanceStack[stackPtr] = rootHit.tMin;
    stackPtr++;

    while (stackPtr > 0u) {
        --stackPtr;

        uint nodeIndex = nodeStack[stackPtr];
        float nodeDistance = distanceStack[stackPtr];
        if (nodeDistance > rayHit.t)
            continue;

        BvhNode node = nodes[nodeIndex];
        bool isLeaf = node.leftChildIndex < 0 && node.rightChildIndex < 0;

        // Find closest triangle in face
        if (isLeaf) {
            for (uint i = 0u; i < node.faceCount; ++i) {
                // ignore casting face
                uint faceIndex = bvhFaceIndices[node.faceOffset + i];
                if (faceIndex == ignoredFaceIndex) continue;
                
                // find closest hit in leaf (ignoring leafs with transparent alpha)
                FaceRayHit candidateHit;
                if (rayIntersectFace(ray, faceIndex, candidateHit) && candidateHit.t < rayHit.t) {
                    
                    Material material = materials[faceMaterials[faceIndex].materialIndex];
                    if (material.alphaMode == 1) {
                        float alpha = material.baseColor.a;
                        if (material.baseColorTextureIndex >= 0) {
                            Face face = faces[faceIndex];
                            vec2 uv = (1.0 - candidateHit.u - candidateHit.v) * vertices[face.vertexIndex0].materialTexCoords
                                    + candidateHit.u * vertices[face.vertexIndex1].materialTexCoords
                                    + candidateHit.v * vertices[face.vertexIndex2].materialTexCoords;
                            alpha *= textureLod(sampler2D(textureHandles[material.baseColorTextureIndex]), uv, 0.0).a;
                        }
                        if (alpha < material.alphaCutoff) continue;
                    }

                    rayHit = candidateHit;
                    if (anyHit) return true; // Shadow rays only need a blocker.
                }
            }
            continue; // no children of leaf
        }

        // Test child AABBs
        uint leftIndex  = uint(node.leftChildIndex);
        uint rightIndex = uint(node.rightChildIndex);
        AabbRayHit leftHit;
        AabbRayHit rightHit;
        bool hitLeft = rayIntersectAabb(ray, nodes[leftIndex].aabb, rayHit.t, leftHit);
        bool hitRight = rayIntersectAabb(ray, nodes[rightIndex].aabb, rayHit.t, rightHit);

        // Missed both children
        if (!hitLeft && !hitRight)
            continue;

        uint pushCount = uint(hitLeft) + uint(hitRight);
        if (stackPtr + pushCount > MAX_STACK_DEPTH) {
            // STACK OVERFLOW ! 
            return false;
        }

        // Add closest children to top of stack
        if(leftHit.tMin < rightHit.tMin) {
            if (hitRight) {
                nodeStack[stackPtr] = rightIndex;
                distanceStack[stackPtr] = rightHit.tMin;
                ++stackPtr;
            }
            if (hitLeft) {
                nodeStack[stackPtr] = leftIndex;
                distanceStack[stackPtr] = leftHit.tMin;
                ++stackPtr;
            }
        } else {
            if (hitLeft) {
                nodeStack[stackPtr] = leftIndex;
                distanceStack[stackPtr] = leftHit.tMin;
                ++stackPtr;
            }
            if (hitRight) {
                nodeStack[stackPtr] = rightIndex;
                distanceStack[stackPtr] = rightHit.tMin;
                ++stackPtr;
            }
        }
    }

    return rayHit.t < INF; // check if we returned because of miss or hit
}

// trace ray into bvh and ignore no faces
bool traceFaceRay(Ray ray, uint bvhRootNodeIndex, out FaceRayHit rayHit, bool anyHit)
{
    return traceFaceRay(ray, bvhRootNodeIndex, rayHit, anyHit, 0xFFFFFFFFu);
}

/*
    Ray Helper functions
*/

vec3 offsetRayOrigin(vec3 position, vec3 normal, vec3 direction, uint faceIndex)
{
    Face face = faces[faceIndex];
    vec3 a = vertices[face.vertexIndex0].position;
    vec3 geometricNormal = normalize(cross(vertices[face.vertexIndex1].position - a, vertices[face.vertexIndex2].position - a));
    if (dot(geometricNormal, normal) < 0.0)    geometricNormal = -geometricNormal;
    if (dot(geometricNormal, direction) < 0.0) geometricNormal = -geometricNormal;
    float offset = max(EPS_RAY, 1e-6 * max(max(abs(position.x), abs(position.y)), abs(position.z)));
    return position + geometricNormal * offset;
}

/*
    Fetching data from ray hit
*/

struct FaceData
{
    vec3 position;
    vec3 normal;
    vec2 materialTexCoord;
    uint materialIndex;
};

FaceData fetchHitFaceData(FaceRayHit hit)
{
    Face face = faces[hit.faceIndex];

    Vertex a = vertices[face.vertexIndex0];
    Vertex b = vertices[face.vertexIndex1];
    Vertex c = vertices[face.vertexIndex2];

    vec3 bary = vec3(1.0 - hit.u - hit.v, hit.u, hit.v);
    FaceData faceData;
    faceData.position =
        bary.x * a.position +
        bary.y * b.position +
        bary.z * c.position;
    faceData.normal = normalize(
        bary.x * a.normal +
        bary.y * b.normal +
        bary.z * c.normal);
    faceData.materialTexCoord =
        bary.x * a.materialTexCoords +
        bary.y * b.materialTexCoords +
        bary.z * c.materialTexCoords;
    faceData.materialIndex = 
        faceMaterials[hit.faceIndex].materialIndex;

    return faceData;
}

/*
    Directional Lights
    -> lights that doen't have 3d extent (only hitable by next event estimation)
*/
struct DirectionalLight
{
    vec3 direction;
    vec3 irradiance;
};

layout(std140, binding = BIND_UBO_LIGHT) uniform Lights
{
    DirectionalLight directionalLights[LIGHT_MAX_COUNT];
};

struct LightRayHit
{
    float lightDistance;
    vec3  lightDirection;
    vec3  lightIrradiance;
};

bool traceLight(uint lightIndex, vec3 position, vec3 normal, out LightRayHit lightHit, uint faceIndex)
{
    DirectionalLight light = directionalLights[lightIndex];
    
    lightHit.lightDirection = -normalize(light.direction);
    lightHit.lightIrradiance = light.irradiance;
    lightHit.lightDistance = INF;

    // Cast shadow ray at light
    Ray shadowRay = Ray(offsetRayOrigin(position, normal, lightHit.lightDirection, faceIndex), lightHit.lightDirection);

    FaceRayHit shadowRayHit;
    bool isVisibleLight = 
        !traceFaceRay(shadowRay, BVH_ROOT_NODE, shadowRayHit, true, faceIndex) 
        || shadowRayHit.t >= lightHit.lightDistance; // a hit could be misleadin if it occured after the ray passed the light
    
    return isVisibleLight;
}

bool traceRandomLight(inout HaltonSeqState state, vec3 position, vec3 normal, out LightRayHit lightHit, uint faceIndex)
{
    if (lightCount == 0u) return false;
    uint lightIndex = min(
        uint(stepHaltonSequence1f(state) * float(lightCount)),
        lightCount - 1u
    );
    return traceLight(lightIndex, position, normal, lightHit, faceIndex);
}

/*
    Directional light maps
    
    -> radiance(x, y, z) = sum_l=0:L sum_m=-l:l [ c_lm * Y_l^m(x, y, z) ]
    -> l := order, m := count

    -> r on unit hemisphere = 1
    Y_0^0 (x, y, z) = 1 / (4*PI)
    Y_1^-1(x, y, z) = sqrt(3/(4*PI)) * (y / r)
    Y_1^0 (x, y, z) = sqrt(3/(4*PI)) * (z / r)
    Y_1^1 (x, y, z) = sqrt(3/(4*PI)) * (x / r)
    ...

    // Solving coefficients by integrating over sphere's surface with r=1
    // intregrate over X: -/+ 1, Y: -/+ sqrt(1-x^2), Z: -/+ sqrt(1-x^2-y^2)
    c_lm = integral_X integral_Y integral_Z [ radiance(x, y, z) * Y_l^m(x, y z) ] dz dy dx
    -> with monte carlo importance sampling := 1/n sum_n=0:N [ radiance(x, y, z) *  Y_l^m(x, y z) / pdf(x, y, z) ]
*/

// degree 0 basis has 1 order
float Y_0()
{
    return sqrt(1.0 / (4.0 * PI));
}

// degree 1 basis has 3 orders
vec3 Y_1(vec3 xyz)
{
    return sqrt(3.0 / (4.0 * PI)) * xyz.yzx;
}

// unqiue spherical harmonic function for each channel
struct ShCoefficient
{
    // c_channel := ( c_0_0, c_1_-1, c_1_0, c_1_1 )
    vec4 c_r;
    vec4 c_g;
    vec4 c_b;
};

layout(std430, binding = BIND_SSBO_L1_SH) buffer ShCoefficients
{
    ShCoefficient shCoefficients[];
};

void updateShCoefficients(inout ShCoefficient shCoeff, vec3 incomingRadiance, vec3 incomingDirection, float samplePdf, uint sampleCount)
{
    float y_0 = Y_0();
    vec3  y_1 = Y_1(incomingDirection);
    vec4  y_channel = vec4(y_0, y_1) / max(samplePdf, EPS);

    // moving average of coefficents
    float invSampelCount = 1.0 / float(sampleCount);
    shCoeff.c_r += invSampelCount * (incomingRadiance.r * y_channel - shCoeff.c_r);
    shCoeff.c_g += invSampelCount * (incomingRadiance.g * y_channel - shCoeff.c_g);
    shCoeff.c_b += invSampelCount * (incomingRadiance.b * y_channel - shCoeff.c_b);
}

/*
    Lightmap Gbuffer Input
*/
uniform sampler2D positionTexture;
uniform sampler2D normalTexture;

/*
    Frame Output
*/
layout(location = FRAME_ATTACHMENT_LIGHT_MAP) out vec4 outIrradiance;


/*
    1-Sample Path Tracing
*/
void main() 
{
    // Initialize pseudo-random number generators
    uvec2 fragCoord = uvec2(gl_FragCoord.xy);
    uint  fragIndex = fragCoord.y * screenWidth + fragCoord.x;

    vec4 positionSample = texelFetch(positionTexture, ivec2(fragCoord), 0);
    if (positionSample.a < 0.5) {
        outIrradiance = vec4(0.0);
        return; // Outside the lightmap UV charts
    }

    PixelState pixelState = pixelStates[fragIndex];
    if (hasConverged(pixelState)) {
        outIrradiance = vec4(pixelState.mean, 1.0);
        return;
    }

    // step new random number
    uint sampleCount = pixelState.count + 1u;
    HaltonSeqState haltonState;
    uint sequenceOffset = (sampleCount - 1u) * (2u + 3u*maxBounces);
    initHaltonSequence(haltonState, sequenceOffset + 1u, sequenceOffset + 1u);
    
    // cranley patterson rotation for more statistical randomness
    vec4 noise = round(texelFetch(noiseTexture, ivec2(fragCoord), 0) * 255.0);
    haltonState.rotation = (noise.rg * 256.0 + noise.ba) / 65536.0;
    
    // Initialize start data for every ray
    vec3  normal   = normalize(texelFetch(normalTexture, ivec2(fragCoord), 0).xyz);
    vec3  position = positionSample.xyz;
    
    // Initial outgoing ray from surface
    float samplePdf;
    vec3  sampleDirection    = sampleHemisphereDirection(haltonState, normal, samplePdf);
    float sampleCosineWeight = max(dot(normal, sampleDirection), 0.0);

    vec3  incomingRadiance = vec3(0.0);
    vec3  throughput       = vec3(1.0);

    /*
        Complete one full outgoing path with bounces
    */
    uint sourceFaceIndex = uint(positionSample.a) - 1u;
    Ray ray = Ray(offsetRayOrigin(position, normal, sampleDirection, sourceFaceIndex), sampleDirection);
    for(uint bounce = 0u; bounce < maxBounces; bounce++) {
        // cast ray
        FaceRayHit rayHit;
        bool isFaceHit = traceFaceRay(ray, BVH_ROOT_NODE, rayHit, false, sourceFaceIndex);
        
        // Environment hit
        if (!isFaceHit)
        {
            vec3 envRadiance = sampleHdri(getHdriTexCoords(ray.direction));
            incomingRadiance += throughput * envRadiance;
            break;
        };

        // Get face data using barycentric interpolation
        FaceData faceData = fetchHitFaceData(rayHit);
        Material material = materials[faceData.materialIndex];
        vec3 albedo = sampleAlbedo(material, faceData.materialTexCoord);
        vec3 brdf   = diffuseBrdf(albedo);

        // flip normal if we hit from the backside of the face
        if (dot(faceData.normal, ray.direction) > 0.0)
            faceData.normal = -faceData.normal;

        // Irradiance from emission on hit surface.
        incomingRadiance += throughput * sampleEmission(material, faceData.materialTexCoord);

        // Irradiance from direct light on hit surface (only can do irradiance because oriententation indepedent diffuse brdf)
        LightRayHit lightHit;
        bool hitLight = traceRandomLight(haltonState, faceData.position, faceData.normal, lightHit, rayHit.faceIndex);
        if (hitLight)
        {
            float lightCosineWeight = max(dot(faceData.normal, lightHit.lightDirection), 0.0);
            float lightPdf          = 1 / float(lightCount);
            incomingRadiance += (throughput * brdf * lightHit.lightIrradiance * lightCosineWeight) / lightPdf;
        }

        // Sample where path goes next
        float bouncePdf;
        vec3  bounceDirection    = sampleHemisphereDirection(haltonState, faceData.normal, bouncePdf);
        float bounceCosineWeight = max(dot(normalize(faceData.normal), bounceDirection), 0.0);

        // Track contribution of outgoing rays from this surface along next path
        throughput *= (brdf * bounceCosineWeight) / bouncePdf;

        // Next ray 
        ray = Ray(offsetRayOrigin(faceData.position, faceData.normal, bounceDirection, rayHit.faceIndex), bounceDirection);
        sourceFaceIndex = rayHit.faceIndex;
    }

    /*
        Wrap up sample
    */
    ShCoefficient shCoeff = shCoefficients[fragIndex];
    float luminance = dot(incomingRadiance, vec3(0.2126, 0.7152, 0.0722));
    incomingRadiance *= min(1.0, MAX_SAMPLE_LUMINANCE / max(luminance, EPS));

    updateShCoefficients(shCoeff, incomingRadiance, sampleDirection, samplePdf, sampleCount);
    shCoefficients[fragIndex] = shCoeff;

    vec3 sampleIrradiance = incomingRadiance * sampleCosineWeight / max(samplePdf, EPS);
    float n = float(sampleCount);
    pixelState.mean += (sampleIrradiance - pixelState.mean) / n;

    // Tracked squared deviation so you can just divide by sample count to get variance
    const vec3 luma = vec3(0.2126, 0.7152, 0.0722);
    float sampleLuminance = dot(sampleIrradiance, luma);
    float delta = sampleLuminance - pixelState.luminanceMean;
    
    pixelState.luminanceMean += delta / n;
    pixelState.M2    = max(pixelState.M2 + delta * (sampleLuminance - pixelState.luminanceMean), 0.0);
    pixelState.count = sampleCount;
    pixelStates[fragIndex] = pixelState;
    
    outIrradiance = vec4(pixelState.mean, 1.0);
}
