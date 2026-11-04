#version 430 core

struct Aabb
{
    vec3 min;
    vec3 max;
};

struct BvhNode
{
    Aabb bounds;

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