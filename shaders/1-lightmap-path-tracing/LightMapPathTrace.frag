#version 430 core
#extension GL_ARB_bindless_texture : require // bindless textures
#extension GL_NV_gpu_shader5 : require       // non-uniform bindless texture access

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

#define BIND_UBO_LIGHT 0u

#define TEX_UNIT_HDRI 6u

#define FRAME_ATTACHMENT_LIGHT_MAP 0u

#define BVH_ROOT_NODE 0u

#define EPS 1e-8f     // general near-zero value
#define EPS_RAY 1e-4f // larger near-zero value to avoid ray-triangle self-intersections
#define INF 1e30
#define NEG_INF (-1e30)
#define PI 3.14159265359
#define MAX_STACK_DEPTH 128u
#define UINT_MAX 0xFFFFFFFFu

#define MAX_SAMPLES 32
#define MAX_BOUNCES 5

/*
    Uniforms
*/
layout (binding = TEX_UNIT_HDRI) uniform sampler2D hdri;
uniform bool hasHdri;
uniform uint screenWidth;
uniform uint screenHeight;
uniform uint lightCount;

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
struct FaceData
{
    uint materialIndex;
};

layout(std430, binding = BIND_SSBO_FACE_DATA) readonly buffer FaceDatum
{
    FaceData faceData[];
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

vec3 sampleAlbedo(Material material, vec2 uv)
{
    vec3 albedo = material.baseColor.rgb;
    if (material.baseColorTextureIndex >= 0)
    {
        uint index = uint(material.baseColorTextureIndex);
        albedo *= texture(sampler2D(textureHandles[index]), uv).rgb;
    }
    return albedo;
}

vec3 sampleEmission(Material material, vec2 uv)
{
    vec3 emission = material.emissive;
    if (material.emissiveTextureIndex >= 0) {
        uint index = uint(material.emissiveTextureIndex);
        emission *= texture(sampler2D(textureHandles[index]), uv).rgb;
    }
    return emission;
}

/*
    Image Based Lighting
    -> Hdri stores radiance given direction in world space
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

vec3 sampleHdri(vec2 uv)
{
    return hasHdri ? texture(hdri, uv).rgb : vec3(0.0);
}

/*
    Singularity Lights
    -> lights that doen't have 3d extent (only hitable by next event estimation)
*/
struct SingularityLight
{
    uint lightType; // 0 := directional, 1 := point
    vec3 position;
    vec3 direction;
    vec3 irradiance;
};

layout(std140, binding = BIND_UBO_LIGHT) uniform Lights
{
    SingularityLight singularityLights[1];
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

layout(std430, binding = BIND_SSBO_BVH_NODES) readonly buffer BvhNodes
{
    BvhNode nodes[];
};

layout(std430, binding = BIND_SSBO_BVH_FACE_INDICES) readonly buffer BvhFaceIndices
{
    uint bvhFaceIndices[];
};

/*
    Uniform Pseudo-Random Numbers
*/
struct HaltonSeqState
{
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
    float value = stepHaltonSequence1f(state);
    uint offset = min(uint(value * float(count)), count - 1u);

    return minValue + offset;
}

/*
    Hemisphere Sampling
*/
vec2 sampleCosineWeightedAngle(inout HaltonSeqState state, out float pdfSample)
{
    // use 2d uniform distribution with values in ( [0, 1), [0, 1) )
    vec2  uniform2d = stepHaltonSequence2f(state);

    // convert 2d uniform into cosine weighted theta and phi direction
    float theta = asin(sqrt(clamp(uniform2d.x, 0.0, 1.0)));
    float phi   = 2 * PI *  uniform2d.y;

    // compute probability of taking that sample direction 
    pdfSample = clamp(cos(theta), EPS, 1.0) / PI;

    return vec2(theta, phi);
}

vec3 sampleCosineWeightedDirection(inout HaltonSeqState state, vec3 normal, out float pdfSample)
{
    // sample phi and theta from pdf (tangent space)
    vec2  thetaPhi = sampleCosineWeightedAngle(state, pdfSample);
    float theta = thetaPhi.x;
    float phi   = thetaPhi.y;

    // get new direction in cartesian coordinates (tangent space)
    vec3 direction = normalize(vec3(
        sin(theta)*cos(phi), 
        sin(theta)*sin(phi), 
        cos(theta)
    ));

    // Get transform from tangent space to world space
    normal = normalize(normal);
    
    bool isParralleltoYaxis = abs(dot(normal, vec3(0, 1, 0))) > 0.999;
    vec3 tangent = normalize( 
        isParralleltoYaxis 
        ? cross(normal, vec3(0, 0, 1)) 
        : cross(normal, vec3(0, 1, 0))
    ); // abitrary perpendicular vector from the cross product of a non-parrallel vector
    
    vec3 bitangent = cross(normal, tangent);

    // transform new direction into world space
    mat3 TBN = mat3(tangent, bitangent, normal);
    return TBN * direction;
}

/*
    Brdf's
*/
vec3 diffuseBrdf(vec3 albedo)
{
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
    vec3  position;
    vec3  normal;
    vec2  materialTexCoord;
    vec2  lightMapTexCoord;
    uint  materialIndex;
    float t;
};

bool rayIntersectFace(Ray ray, uint faceIndex, out FaceRayHit rayHit)
{
    Face face = faces[faceIndex];
    Vertex vertA = vertices[face.vertexIndex0];
    Vertex vertB = vertices[face.vertexIndex1];
    Vertex vertC = vertices[face.vertexIndex2];

    vec3 T = vec3(ray.origin - vertA.position);
    mat3 M = mat3(-ray.direction, vertB.position - vertA.position, vertC.position - vertA.position);
    
    // Check if ray is parrallel to plane
    float detM = determinant(M);

    // Cramers rule says no uniqne solution exists
    if (abs(detM) < EPS) {
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

    // Barycentric interpolation of vertex attributes.
    vec3 baryCoords = vec3(1.0 - u - v, u, v);
    rayHit.position =
        baryCoords.x * vertA.position +
        baryCoords.y * vertB.position +
        baryCoords.z * vertC.position;

    rayHit.materialTexCoord =
        baryCoords.x * vertA.materialTexCoords +
        baryCoords.y * vertB.materialTexCoords +
        baryCoords.z * vertC.materialTexCoords;

    rayHit.lightMapTexCoord =
        baryCoords.x * vertA.lightMapTexCoords +
        baryCoords.y * vertB.lightMapTexCoords +
        baryCoords.z * vertC.lightMapTexCoords;

    rayHit.normal = normalize(
        baryCoords.x * vertA.normal +
        baryCoords.y * vertB.normal +
        baryCoords.z * vertC.normal
    );
    rayHit.materialIndex = faceData[faceIndex].materialIndex;
    rayHit.t = t;
    return true;
}

bool rayIntersectAabb(Ray ray, Aabb aabb)
{
    bvec3 parallel = equal(ray.direction, vec3(0.0));
    vec3  invDirection = 1 / mix(ray.direction, vec3(1.0), parallel);

    // compute when/if ray intersects aabb
    vec3 t_xyz_low  = (aabb.min - ray.origin) * invDirection;
    vec3 t_xyz_high = (aabb.max - ray.origin) * invDirection;
    for (int i = 0; i < 3; ++i) {
        if (!parallel[i]) continue;
        if (ray.origin[i] < aabb.min[i] || ray.origin[i] > aabb.max[i]) return false;
        t_xyz_low[i] = -INF;
        t_xyz_high[i] = INF;
    }

    vec3 t_xyz_enter = min(t_xyz_low, t_xyz_high);
    vec3 t_xyz_exit  = max(t_xyz_low, t_xyz_high);
    float t_enter = max(max(t_xyz_enter.x, t_xyz_enter.y), t_xyz_enter.z);
    float t_exit  = min(min(t_xyz_exit.x, t_xyz_exit.y), t_xyz_exit.z);

    bool isMiss   = t_enter > t_exit;
    bool isBehind = t_exit < 0.0f;
    bool isHit = !isMiss && !isBehind;
    return isHit;
}

bool traceRay(Ray ray, uint bvhRootNodeIndex, out FaceRayHit rayHit)
{
    BvhNode nodeStack[MAX_STACK_DEPTH];
    uint    stackPtr = 0;

    // Root Intersection
    BvhNode rootNode = nodes[bvhRootNodeIndex];
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
            FaceRayHit candidateRayHit;
            for(uint i = 0; i < node.faceCount; i++) {
                uint faceIndex = bvhFaceIndices[node.faceOffset + i];
                hitFace = rayIntersectFace(ray, faceIndex, candidateRayHit);
                rayHit = hitFace && candidateRayHit.t < rayHit.t ? candidateRayHit : rayHit;  
            }
        }

        // Add child nodes to stack
        bool hasNoStackOverflow = stackPtr+2 <= MAX_STACK_DEPTH;
        if (!isLeaf && isHit && hasNoStackOverflow) {
            nodeStack[stackPtr++] = nodes[node.leftChildIndex];
            nodeStack[stackPtr++] = nodes[node.rightChildIndex];
        }
    }
    return rayHit.t < INF ? true : false;
}

/*
    Light-Source Ray Intersections
*/
struct LightRayHit
{
    float lightDistance;
    vec3  lightDirection;
    vec3  lightIrradiance;
};

bool traceRandomLight(inout HaltonSeqState state, vec3 position, vec3 normal, out LightRayHit lightHit)
{
    if (lightCount == 0u) return false;

    // Uniformly select random light source
    uint lightIndex = min(
        uint(stepHaltonSequence1f(state) * float(lightCount)),
        lightCount - 1u
    );
    SingularityLight light = singularityLights[lightIndex];
    
    // Sample Directional Light
    if (light.lightType == 0u)
    {
        lightHit.lightDirection = -normalize(light.direction);
        lightHit.lightIrradiance = light.irradiance;
        lightHit.lightDistance   = INF;
    }
    // Sample Point Light
    else 
    {
        vec3  lightDirection = light.position - position;
        float lightDistance2 = dot(lightDirection, lightDirection);
        float lightDistance = sqrt(lightDistance2);
        if (lightDistance2 < EPS) return false; // Light is inside face 

        lightHit.lightDirection  = lightDirection / lightDistance;
        lightHit.lightIrradiance = light.irradiance / lightDistance2;
        lightHit.lightDistance   = lightDistance; 
    }

    // Light is behind face
    if (dot(normal, lightHit.lightDirection) < EPS) {
        return false;
    }

    // Cast shadow ray at light
    Ray shadowRay = Ray(
        position + normalize(normal) * EPS_RAY, // offset slightly outside surface
        lightHit.lightDirection
    );
    float shadowDistance = lightHit.lightDistance;
    if (light.lightType != 0u) {
        vec3 toLight = light.position - shadowRay.origin;
        shadowDistance = length(toLight);
        if (shadowDistance == 0.0) return true;
        shadowRay.direction = toLight / shadowDistance;
    }
    FaceRayHit shadowRayHit;
    bool hitObstacle = 
        traceRay(shadowRay, BVH_ROOT_NODE, shadowRayHit) &&
        shadowRayHit.t < shadowDistance;
    
    // Return true if it hit the light
    return !hitObstacle;
}

/*
    Vertex Shader Input
*/
in vec3 fragPosition;
in vec3 fragNormal;

/*
    Frame Output
*/
layout (location = FRAME_ATTACHMENT_LIGHT_MAP) out vec3 outIrradiance;

// to do add shadow mask
layout (location = FRAME_ATTACHMENT_SHADOW_MASK) out vec3 outShadowMask;

void main() 
{
    // Initialize pseudo-random number generators
    uvec2 fragCoord = uvec2(gl_FragCoord.xy);
    uint  fragIndex = fragCoord.y * screenWidth + fragCoord.x;
    HaltonSeqState haltonState;
    initHaltonSequence(haltonState, fragIndex, fragIndex+1u);
    
    // Initialize irradiance accumulated over all rays
    vec3  irradiance  = vec3(0.0);

    // Trace rays
    vec3  normal   = normalize(fragNormal);
    vec3  position = fragPosition; 
    for(int i = 0; i < MAX_SAMPLES; i++) {
        // Direct light on lightmap surface 
        // (Don't compute this if you do direct lighting as second pass)
        LightRayHit directHit;
        bool isDirectHit = traceRandomLight(haltonState, position, normal, directHit);
        if (isDirectHit) {
            float lightCosineWeight = max(dot(normal, directHit.lightDirection), 0.0);
            float lightSamplePdf    = 1 / float(lightCount);
            irradiance += (directHit.lightIrradiance * lightCosineWeight) / lightSamplePdf;
        }

        // Initial outgoing ray from surface
        float samplePdf;
        vec3  sampleDirection = sampleCosineWeightedDirection(haltonState, normal, samplePdf);
        float sampleCosineWeight = max(dot(normal, sampleDirection), 0.0);
        vec3  throughput   = vec3(sampleCosineWeight / samplePdf);

        // Traverse outgoing bounces
        Ray ray = Ray(position + normal * EPS_RAY, sampleDirection);
        for(int bounce = 0; bounce < MAX_BOUNCES; bounce++) {
            // Shoot ray
            FaceRayHit rayHit;
            bool isHit = traceRay(ray, BVH_ROOT_NODE, rayHit);
            
            // Environment hit
            if (!isHit)
            {
                vec3 envRadiance = sampleHdri(getHdriTexCoords(ray.direction));
                irradiance += throughput * envRadiance;
                break;
            }

            // Mesh hit
            Material material = materials[rayHit.materialIndex];
            vec3 albedo = sampleAlbedo(material, rayHit.materialTexCoord);
            vec3 brdf   = diffuseBrdf(albedo);

            // Irradiance from emission on hit surface.
            irradiance += throughput * sampleEmission(material, rayHit.materialTexCoord);

            // Irradiance from direct light on hit surface
            LightRayHit lightHit;
            bool hitLight = traceRandomLight(haltonState, rayHit.position, rayHit.normal, lightHit);
            if (hitLight)
            {
                float lightCosineWeight = max(dot(rayHit.normal, lightHit.lightDirection), 0.0);
                float lightSamplePdf    = 1 / float(lightCount);
                irradiance += 
                    (throughput * brdf * lightHit.lightIrradiance * lightCosineWeight) 
                    / lightSamplePdf;
            }

            // Sample where path goes next
            sampleDirection    = sampleCosineWeightedDirection(haltonState, rayHit.normal, samplePdf);
            sampleCosineWeight = max(dot(normalize(rayHit.normal), sampleDirection), 0.0);
            ray = Ray(rayHit.position + rayHit.normal * EPS_RAY, sampleDirection);

            // Track radiance weighted by throughput along this path
            throughput *= (brdf * sampleCosineWeight) / samplePdf;
        }
    }

    outIrradiance = irradiance / MAX_SAMPLES;
}
