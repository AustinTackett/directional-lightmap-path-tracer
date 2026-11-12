#version 430 core
layout(early_fragment_tests) in;

/*
    Constants
*/
#define BIND_SSBO_BVH_NODES 0u
#define BIND_SSBO_BVH_FACE_INDICES 1u
#define BIND_SSBO_VERTICES 2u
#define BIND_SSBO_FACES 3u
#define BIND_SSBO_SHADOW_MASK 7u

#define BIND_UBO_LIGHT 0u


#define FRAME_ATTACHMENT_SHADOW_MASK 0u

#define BVH_ROOT_NODE 0u

#define LIGHT_MAX_COUNT 4u

#define EPS 1e-8f     // general near-zero value
#define EPS_RAY 1e-4f // larger near-zero value to avoid ray-triangle self-intersections
#define INF 1e30
#define NEG_INF (-1e30)
#define PI 3.14159265359
#define MAX_STACK_DEPTH 64u
#define UINT_MAX 0xFFFFFFFFu


/*
    Uniforms
*/
uniform uint screenWidth;
uniform uint screenHeight;
uniform uint lightCount;

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
    Rays
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
    Lights
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

in vec3 fragPosition;
in vec3 fragNormal;

/*
    Frame Output
*/
layout (location = FRAME_ATTACHMENT_SHADOW_MASK) out vec4 outIrradiance;
layout(std430, binding = BIND_SSBO_SHADOW_MASK)  buffer ShadowMask
{
    uint shadowMask[];
};

void setLightVisible(uint texelIndex, uint lightIndex) {
    uint wordsPerTexel = (lightCount + 31u) / 32u;
    uint wordIndex = texelIndex * wordsPerTexel + lightIndex / 32u;
    atomicOr(shadowMask[wordIndex], 1u << (lightIndex % 32u));
}

bool isLightVisible(uint texelIndex, uint lightIndex)
{
    uint wordsPerTexel = (lightCount + 31u) / 32u;
    uint wordIndex = lightIndex / 32u;
    uint bitIndex = lightIndex % 32u;
    uint mask = shadowMask[texelIndex * wordsPerTexel + wordIndex];
    return (mask & (1u << bitIndex)) != 0u;
}

void main()
{
    uvec2 fragCoord = uvec2(gl_FragCoord.xy);
    uint fragIndex = fragCoord.y * screenWidth + fragCoord.x;
    vec3 normal = normalize(fragNormal);
    vec3 position = fragPosition;
    vec3 directLightIrradiance = vec3(0.0);

    // Compute direct lighting and Store visibility for each light
    for (uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex) {
        LightRayHit visibilityHit;
        bool visible = traceLight(lightIndex, position, normal, visibilityHit);
        if (visible) {
            setLightVisible(fragIndex, lightIndex);
            float cosineWeight = max(dot(normal, visibilityHit.lightDirection), 0.0);
            directLightIrradiance += visibilityHit.lightIrradiance * cosineWeight;
        }
    }

    outIrradiance = vec4(directLightIrradiance, 1.0);
}
