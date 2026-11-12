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
#define BIND_SSBO_SHADOW_MASK 7u

#define BIND_UBO_LIGHT 0u

#define TEX_UNIT_LIGHT_MAP 0u
#define TEX_UNIT_SHADOW_MASK 1u
#define TEX_UNIT_HDRI 6u
#define TEX_UNIT_NOISE 7u

#define FRAME_ATTACHMENT_LIGHT_MAP 0u

#define BVH_ROOT_NODE 0u

#define LIGHT_MAX_COUNT 4u

#define EPS 1e-8f     // general near-zero value
#define EPS_RAY 1e-4f // larger near-zero value to avoid ray-triangle self-intersections
#define INF 1e30
#define NEG_INF (-1e30)
#define PI 3.14159265359
#define MAX_STACK_DEPTH 64u
#define UINT_MAX 0xFFFFFFFFu

#define MAX_BOUNCES 2

/*
    Uniforms
*/
layout (binding = TEX_UNIT_HDRI)      uniform sampler2D hdri;
layout (binding = TEX_UNIT_LIGHT_MAP) uniform sampler2D previousLightMap;
layout (binding = TEX_UNIT_NOISE)     uniform sampler2D noiseTexture;
layout (binding = TEX_UNIT_SHADOW_MASK) uniform sampler2D shadowMask;
uniform uint pixelSampleCount;
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
    if (material.baseColorTextureIndex >= 0) {
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
        i1 = i1 / state.base1; // truncates
    }
    while (i2 > 0u)
    {
        f2 = f2 / float(state.base2);
        r2 = r2 + f2 * float(i2 % state.base2);
        i2 = i2 / state.base2; // truncates
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
        i1 = i1 / state.base1; // truncates
    }

    return fract(r1 + state.rotation.x);
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
    uint  faceIndex;
    float u;
    float v;
    float t;
};

bool rayIntersectFace(Ray ray, uint faceIndex, out FaceRayHit faceHit)
{
    faceHit.faceIndex = faceIndex;
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

    faceHit.t = determinant(Mx)/detM;
    faceHit.u = determinant(My)/detM;
    faceHit.v = determinant(Mz)/detM;

    // Check for miss (u and v indicate its outside of triangle face)
    if (faceHit.u < 0.0f || faceHit.v < 0.0f || faceHit.u + faceHit.v > 1.0f)
        return false;
    // check if triangle was behind ray (t is a negative distance)
    if (faceHit.t <= EPS)
        return false;

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

    for (int axis = 0; axis < 3; ++axis) {
        float origin = ray.origin[axis];
        float direction = ray.direction[axis];

        if (direction == 0.0) {
            if (origin < aabb.min[axis] || origin > aabb.max[axis]) {
                return false;
            }
            continue;
        }

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

bool traceFaceRay(Ray ray, uint bvhRootNodeIndex, out FaceRayHit rayHit)
{
    uint nodeStack[MAX_STACK_DEPTH];
    float distanceStack[MAX_STACK_DEPTH];
    uint stackPtr = 0u;

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
                uint faceIndex = bvhFaceIndices[node.faceOffset + i];
                FaceRayHit candidateHit;
                if (rayIntersectFace(ray, faceIndex, candidateHit) && candidateHit.t < rayHit.t) {
                    rayHit = candidateHit;
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
            // STACK OVERFLOW ;)
            return false;
        }

        // Add children to stack
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

    return rayHit.t < INF;
}

bool traceVisibility(Ray ray, uint bvhRootNodeIndex, float maxT)
{
    uint  nodeStack[MAX_STACK_DEPTH];
    uint  stackPtr = 0u;

    // Test root AABB
    AabbRayHit rootHit;
    if (!rayIntersectAabb(ray, nodes[bvhRootNodeIndex].aabb, maxT, rootHit)) {
        return true;
    }

    nodeStack[stackPtr++] = bvhRootNodeIndex;
    while (stackPtr > 0u) {
        uint nodeIndex = nodeStack[--stackPtr];
        BvhNode node = nodes[nodeIndex];

        bool isLeaf = node.leftChildIndex < 0 && node.rightChildIndex < 0;

        // Find closest triangle in face
        if (isLeaf) {
            FaceRayHit faceHit;
            for (uint i = 0u; i < node.faceCount; ++i) {
                uint faceIndex = bvhFaceIndices[node.faceOffset + i];
                if (rayIntersectFace(ray, faceIndex, faceHit) && faceHit.t < maxT) {
                    return false;
                }
            }
            continue; // no children of leaf
        }

        // Test child AABBs
        uint leftIndex  = uint(node.leftChildIndex);
        uint rightIndex = uint(node.rightChildIndex);
        AabbRayHit leftHit;
        AabbRayHit rightHit;
        bool hitLeft = rayIntersectAabb(ray, nodes[leftIndex].aabb, maxT, leftHit);
        bool hitRight = rayIntersectAabb(ray, nodes[rightIndex].aabb, maxT, rightHit);

        // Missed both children
        if (!hitLeft && !hitRight)
            continue;

        // Add children to stack
        if (hitLeft && stackPtr+1 <= MAX_STACK_DEPTH) {
            nodeStack[stackPtr] = leftIndex;
            ++stackPtr;
        }
        if (hitRight && stackPtr+1 <= MAX_STACK_DEPTH) {
            nodeStack[stackPtr] = rightIndex;
            ++stackPtr;
        }
    }

    return true;
}

/*
    Getting data from face hit
*/

struct FaceData
{
    vec3 position;
    vec3 normal;
    vec2 materialTexCoord;
    vec2 lightMapTexCoord;
    uint materialIndex;
};

FaceData getFaceData(FaceRayHit hit)
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
    faceData.lightMapTexCoord =
        bary.x * a.lightMapTexCoords +
        bary.y * b.lightMapTexCoords +
        bary.z * c.lightMapTexCoords;
    faceData.materialIndex = 
        faceMaterials[hit.faceIndex].materialIndex;

    return faceData;
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
    SingularityLight singularityLights[LIGHT_MAX_COUNT];
};

struct LightRayHit
{
    float lightDistance;
    vec3  lightDirection;
    vec3  lightIrradiance;
};

bool traceLight(uint lightIndex, vec3 position, vec3 normal, out LightRayHit lightHit)
{
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
    Ray shadowRay = Ray(position + normalize(normal) * EPS_RAY, lightHit.lightDirection);

    FaceRayHit shadowRayHit;
    bool isVisibleLight = traceVisibility(shadowRay, BVH_ROOT_NODE, lightHit.lightDistance);
    
    return isVisibleLight;
}

bool traceRandomLight(inout HaltonSeqState state, vec3 position, vec3 normal, out LightRayHit lightHit)
{
    if (lightCount == 0u) return false;
    uint lightIndex = min(
        uint(stepHaltonSequence1f(state) * float(lightCount)),
        lightCount - 1u
    );
    return traceLight(lightIndex, position, normal, lightHit);
}

/*
    Vertex Shader Input
*/
in vec3 fragPosition;
in vec3 fragNormal;

/*
    Frame Output
*/
layout (location = FRAME_ATTACHMENT_LIGHT_MAP)  out vec4 outIrradiance;
void main() 
{
    // Initialize pseudo-random number generators
    uvec2 fragCoord = uvec2(gl_FragCoord.xy);
    uint  fragIndex = fragCoord.y * screenWidth + fragCoord.x;
    
    HaltonSeqState haltonState;
    uint sequenceOffset = pixelSampleCount * uint(1 + 2*MAX_BOUNCES);
    initHaltonSequence(haltonState, sequenceOffset + 1u, sequenceOffset + 1u);
    
    vec4 noise = round(texelFetch(noiseTexture, ivec2(fragCoord), 0) * 255.0);
    haltonState.rotation = (noise.rg * 256.0 + noise.ba) / 65536.0;
    
    // Initialize start data for every ray
    vec3  normal     = normalize(fragNormal);
    vec3  position   = fragPosition;
    vec3  irradiance = vec3(0.0);
    vec3  directLightIrradiance = texelFetch(shadowMask, ivec2(fragCoord), 0).rgb;
    
    // Initial outgoing ray from surface
    float samplePdf;
    vec3  sampleDirection    = sampleCosineWeightedDirection(haltonState, normal, samplePdf);
    float sampleCosineWeight = max(dot(normal, sampleDirection), 0.0);
    vec3  throughput         = vec3(sampleCosineWeight / samplePdf);

    // Traverse outgoing bounces
    Ray ray = Ray(position + normal * EPS_RAY, sampleDirection);
    for(int bounce = 0; bounce < MAX_BOUNCES; bounce++) {
        // Shoot ray
        FaceRayHit rayHit;
        bool isFaceHit = traceFaceRay(ray, BVH_ROOT_NODE, rayHit);
        
        // Environment hit
        if (!isFaceHit)
        {
            vec3 envRadiance = sampleHdri(getHdriTexCoords(ray.direction));
            irradiance += throughput * envRadiance;
            break;
        };

        // Get face data using barycentric interpolation
        FaceData faceData = getFaceData(rayHit);
        Material material = materials[faceData.materialIndex];
        vec3 albedo = sampleAlbedo(material, faceData.materialTexCoord);
        vec3 brdf   = diffuseBrdf(albedo);

        // Irradiance from emission on hit surface.
        irradiance += throughput * sampleEmission(material, faceData.materialTexCoord);

        // Irradiance from direct light on hit surface
        LightRayHit lightHit;
        bool hitLight = traceRandomLight(haltonState, faceData.position, faceData.normal, lightHit);
        if (hitLight)
        {
            float lightCosineWeight = max(dot(faceData.normal, lightHit.lightDirection), 0.0);
            float lightSamplePdf    = 1 / float(lightCount);
            irradiance += 
                (throughput * brdf * lightHit.lightIrradiance * lightCosineWeight) 
                / lightSamplePdf;
        }

        // Sample where path goes next
        sampleDirection    = sampleCosineWeightedDirection(haltonState, faceData.normal, samplePdf);
        sampleCosineWeight = max(dot(normalize(faceData.normal), sampleDirection), 0.0);
        ray = Ray(faceData.position + faceData.normal * EPS_RAY, sampleDirection);

        // Track radiance weighted by throughput along this path
        throughput *= (brdf * sampleCosineWeight) / samplePdf;
    }

    vec3 previousIrradiance = texelFetch(previousLightMap, ivec2(fragCoord), 0).rgb;
    irradiance += directLightIrradiance;
    irradiance = previousIrradiance + (irradiance - previousIrradiance) / float(pixelSampleCount + 1u);
    outIrradiance = vec4(irradiance, 1.0);
}
