#version 430 core

/*
    Face data
*/
struct Face
{
    uint vertexIndex0;
    uint vertexIndex1;
    uint vertexIndex2;
}

layout(std430, binding = 3) readonly buffer Faces
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

layout(std430, binding = 2) readonly buffer Vertices
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

layout(std430, binding = 0) readonly buffer BvhNodes
{
    BvhNode nodes[];
};

layout(std430, binding = 1) readonly buffer BvhFaceIndices
{
    uint faceIndices[];
};

/*
    Ray bvh traversal
*/
struct Ray
{
    vec3  origin;
    vec3  direction;
};

struct RayHit
{
    bool isHit;
    vec3 normal;
    vec3 position;
};

bool rayIntersectFace() 
{

};

bool rayIntersectAabb(Ray ray, Aabb aabb)
{

};

RayHit rayTraverseBvh(Ray ray, uint bvhRootNodeIndex)
{
    // if hits root add both children to stack
    // for each children see if it intersects
    // for each child that intersects add its children
    // if you are a leaf node check all faces (may not always contain face)
    const uint maxStackDepth = 128;
    BvhNode nodeStack[maxStackDepth];
    uint    stackPtr = 0;

    BvhNode rootNode = bvhNodes[bvhRootNodeIndex];
    bool hitRoot = rayIntersectAabb(ray, rootNode.aabb);
    if (hitRoot) { 
        return false;
    }

    nodeStack[stackPtr] = rootNode;
    stackPtr++;
    while (stackPtr > 0) {
        BvhNode node = nodeStack[stackPtr];
        stackPtr--;

        bool isHit  = rayIntersectAabb(ray, node.aabb);
        bool isLeaf = node.leftChildIndex > 0 && node.rightChildIndex > 0;

        if (isLeaf) {
            // check all faces and set face
            // and return true if there is a face intersection
        }

        if (hit) {
            nodeStack[stackPtr] = bvhNodes[node.leftChildIndex];
            stackPtr++;
            nodeStack[stackPtr] = bvhNodes[node.rightChildIndex];
            stackPtr++;
        }
    }
    return false;
};

in vec3 fragPosition;
in vec3 fragNormal;

layout (location = 0) out vec3 fragOutputIrradiance; // color attachment 0

void main() 
{


    fragOutputIrradiance = vec3(0, 0, 0);
}