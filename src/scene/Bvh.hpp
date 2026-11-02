#pragma once 

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stack>
#include <vector>

#include <glm/common.hpp>
#include <glm/vec3.hpp>

#include "Mesh.hpp"

/*
    Bvh Data std430 Aligned SSBO's
    -> std430 
        1. float/uint/int/bool 4 bytes
        2. vec2 8 bytes
        3. vec3/vec4 16 bytes
*/

constexpr unsigned int TARGET_LEAF_FACE_COUNT = 32;
constexpr unsigned int BINS_PER_SPLIT    = 16;

struct alignas(16) Aabb
{
    alignas(16) glm::vec3 min = glm::vec3 { std::numeric_limits<float>::max()    };
    alignas(16) glm::vec3 max = glm::vec3 { std::numeric_limits<float>::lowest() };
};

struct alignas(16) BvhNode
{
    Aabb bounds;

    // face indices in bvh class not mesh
    unsigned int faceOffset;
    unsigned int faceCount;

    int leftChildIndex  = -1;
    int rightChildIndex = -1;
};

struct Bvh
{
    std::vector<BvhNode>      nodes;
    std::vector<unsigned int> faceIndices;
};

// Make sure alignment fits std430
static_assert(sizeof(Aabb) == 32);
static_assert(offsetof(Aabb, max) == 16);

static_assert(sizeof(BvhNode) == 48);
static_assert(offsetof(BvhNode, faceOffset) == 32);
static_assert(offsetof(BvhNode, rightChildIndex) == 44);

static BvhNode CreateBvhNode(const Bvh& bvh, const Mesh& mesh, unsigned int faceOffset, unsigned int faceCount);
static bool SplitBvhNode(Bvh& bvh, unsigned int nodeIndex, const Mesh& mesh);

bool BuildBvh(Bvh& bvh, Mesh& mesh) {
    // Check Mesh if Valid
    std::cout << "Building Bvh" << "\n";
    if (mesh.faceVertexIndices.empty()) {
        std::cout << "Mesh had no faces to construct BVH" << "\n";
        return false;
    }

    // Initialize bvh indices
    bvh.nodes.clear();
    bvh.faceIndices.clear();
    for (unsigned int i = 0; i < mesh.faceVertexIndices.size(); ++i) {
        bvh.faceIndices.push_back(i);
    }

    // Create Root Node
    bvh.nodes.push_back(CreateBvhNode(bvh,
            mesh,
            0,
            static_cast<unsigned int>(bvh.faceIndices.size())
    ));

    // Construct Tree Top Down from Root
    std::stack<unsigned int> frontierNodeIndices;
    frontierNodeIndices.push(0);
    while (!frontierNodeIndices.empty()) {
        const unsigned int nodeIndex = frontierNodeIndices.top();
        frontierNodeIndices.pop();

        // Create child nodes until you reach terminal condition
        if (SplitBvhNode(bvh, nodeIndex, mesh)) {
            frontierNodeIndices.push(bvh.nodes[nodeIndex].rightChildIndex);
            frontierNodeIndices.push(bvh.nodes[nodeIndex].leftChildIndex);
        }
    }

    std::cout << "Bvh built with " << bvh.nodes.size() << " nodes" << "\n";
    return true;
}


/*
    Constructing Bvh Functions
*/

// create bounds manually specifying bounds
static Aabb CreateAabb(glm::vec3 min, glm::vec3 max)
{
    return Aabb{ min, max };
}

// Bound a contiguous range of mesh faces.
static Aabb CreateAabb(const Mesh& mesh, unsigned int faceStart, unsigned int faceCount)
{
    Aabb bounds;
    for (unsigned int faceIndex = faceStart; faceIndex < faceStart+faceCount; faceIndex++) {
        const FaceVertexIndices& face = mesh.faceVertexIndices[faceIndex];
        const VertexData& vertex0 = mesh.vertexData[face.vertexIndex0];
        const VertexData& vertex1 = mesh.vertexData[face.vertexIndex1];
        const VertexData& vertex2 = mesh.vertexData[face.vertexIndex2];
        bounds.min = glm::min(bounds.min, glm::min(vertex0.position, glm::min(vertex1.position, vertex2.position)));
        bounds.max = glm::max(bounds.max, glm::max(vertex0.position, glm::max(vertex1.position, vertex2.position)));
    }
    return bounds;
}

// create bounds contigous indices on bvh
static Aabb CreateAabb(const Mesh& mesh, const Bvh& bvh, unsigned int faceOffset, unsigned int faceCount)
{
    Aabb bounds;
    for (unsigned int faceSlot = faceOffset; faceSlot < faceOffset+faceCount; faceSlot++) {
        const FaceVertexIndices& face = mesh.faceVertexIndices[bvh.faceIndices[faceSlot]];
        VertexData vertex0 = mesh.vertexData[face.vertexIndex0];
        VertexData vertex1 = mesh.vertexData[face.vertexIndex1];
        VertexData vertex2 = mesh.vertexData[face.vertexIndex2];
        bounds.min = glm::min(bounds.min, glm::min(vertex0.position, glm::min(vertex1.position, vertex2.position)));
        bounds.max = glm::max(bounds.max, glm::max(vertex0.position, glm::max(vertex1.position, vertex2.position)));
    }
    return bounds;
}

// create bounds using random indices on bvh
static Aabb CreateAabb(const Mesh& mesh, const Bvh& bvh, const std::vector<unsigned int>& faceSlots)
{
    Aabb bounds;
    for (unsigned int faceSlot : faceSlots) {
        const FaceVertexIndices& face = mesh.faceVertexIndices[bvh.faceIndices[faceSlot]];
        VertexData vertex0 = mesh.vertexData[face.vertexIndex0];
        VertexData vertex1 = mesh.vertexData[face.vertexIndex1];
        VertexData vertex2 = mesh.vertexData[face.vertexIndex2];
        bounds.min = glm::min(bounds.min, glm::min(vertex0.position, glm::min(vertex1.position, vertex2.position)));
        bounds.max = glm::max(bounds.max, glm::max(vertex0.position, glm::max(vertex1.position, vertex2.position)));
    }
    return bounds;
}

static float GetAabbSurfaceArea(const Aabb& bounds)
{
    glm::vec3 extents = bounds.max - bounds.min;
    if (extents.x < 0 || extents.y < 0 || extents.z < 0) return 0;
    return 2.0f * (extents.x * extents.y + extents.y * extents.z + extents.z * extents.x);
};

/*
    Bounding Volume Hierarchy Functiosn
*/

static unsigned int LongestAxis(glm::vec3 extents) {
    if (extents.x >= extents.y && extents.x >= extents.z) return 0;
    if (extents.y >= extents.z) return 1;
    return 2;
}

static BvhNode CreateBvhNode(Aabb bounds, unsigned int faceOffset, unsigned int faceCount)
{
    return BvhNode{
        bounds,
        faceOffset,
        faceCount,
        -1,
        -1
    };
}

static BvhNode CreateBvhNode(const Bvh& bvh, const Mesh& mesh, unsigned int faceOffset, unsigned int faceCount)
{
    return BvhNode{
        CreateAabb(mesh, bvh, faceOffset, faceCount),
        faceOffset,
        faceCount,
        -1,
        -1
    };
}

static bool SplitBvhNode(Bvh& bvh, unsigned int nodeIndex, const Mesh& mesh)
{
    const BvhNode node = bvh.nodes[nodeIndex];
    if (node.faceCount <= TARGET_LEAF_FACE_COUNT) return false;

    // Split along the longest axis of the centroid bounds.
    glm::vec3 centroidMin = glm::vec3 { std::numeric_limits<float>::max()    };
    glm::vec3 centroidMax = glm::vec3 { std::numeric_limits<float>::lowest() };
    for (unsigned int faceSlot = node.faceOffset; faceSlot < node.faceOffset+node.faceCount; ++faceSlot) {
        const FaceData& face = mesh.faceData[bvh.faceIndices[faceSlot]];
        centroidMin = glm::min(centroidMin, face.centroid);
        centroidMax = glm::max(centroidMax, face.centroid);
    }
    glm::vec3 centroidExtents = centroidMax - centroidMin;
    unsigned int splitAxis = LongestAxis(centroidExtents);
    const float parentSurfaceArea = GetAabbSurfaceArea(node.bounds);
    if (centroidExtents[splitAxis] <= 0 || parentSurfaceArea <= 0) return false;

    // Group face slots into equal-width bins along the split axis.
    auto FindBinIndex = [&](float coordinate) -> unsigned int {
        float normalized = (coordinate - centroidMin[splitAxis]) /
            (centroidMax[splitAxis] - centroidMin[splitAxis]);
        unsigned int binIndex = static_cast<unsigned int>(normalized * BINS_PER_SPLIT);

        return std::min(binIndex, BINS_PER_SPLIT - 1);
    };
    std::array<std::vector<unsigned int>, BINS_PER_SPLIT> binFaceSlots;
    for (unsigned int faceSlot = node.faceOffset; faceSlot < node.faceOffset+node.faceCount; ++faceSlot) {
        const glm::vec3& centroid = mesh.faceData[bvh.faceIndices[faceSlot]].centroid;
        unsigned int bin = FindBinIndex(centroid[splitAxis]);
        binFaceSlots[bin].push_back(faceSlot);
    }

    // Calculate Aabb for each Bin
    std::array<Aabb, BINS_PER_SPLIT> binBounds;
    for (unsigned int i = 0; i < BINS_PER_SPLIT; ++i) {
        const std::vector<unsigned int>& faceSlots = binFaceSlots[i];
        if (!faceSlots.empty()) {
            binBounds[i] = CreateAabb(mesh, bvh, faceSlots);
        }
    }

    // Functions to combine aabbs that encapsulate each bin and count faces in each bin
    auto CombineBinBounds = [&](unsigned int binStart, unsigned int binEnd) -> Aabb {
        Aabb bounds;
        for (unsigned int binIndex = binStart; binIndex < binEnd; ++binIndex) {
            bounds.min = glm::min(bounds.min, binBounds[binIndex].min);
            bounds.max = glm::max(bounds.max, binBounds[binIndex].max);
        }
        return bounds;
    };
    auto CountBinFaces = [&](unsigned int binStart, unsigned int binEnd) -> unsigned int {
        unsigned int count = 0;
        for (unsigned int binIndex = binStart; binIndex < binEnd; ++binIndex) {
            count += static_cast<unsigned int>(binFaceSlots[binIndex].size());
        }
        return count;
    };

    // Each candidate boundary is the first bin belonging to the right child.
    Aabb bestLeftBounds;
    Aabb bestRightBounds;
    float bestSplitCost = static_cast<float>(node.faceCount);
    int   bestSplitBin  = 0;
    for (unsigned int binIndex = 1; binIndex < BINS_PER_SPLIT; ++binIndex) {
        Aabb leftBounds  = CombineBinBounds(0, binIndex);
        Aabb rightBounds = CombineBinBounds(binIndex, BINS_PER_SPLIT);
        unsigned int leftFaceCount  = CountBinFaces(0, binIndex);
        unsigned int rightFaceCount= CountBinFaces(binIndex, BINS_PER_SPLIT);
        if (leftFaceCount == 0 || rightFaceCount == 0) continue;
        float splitCost = 1 +
            (GetAabbSurfaceArea(rightBounds) / GetAabbSurfaceArea(node.bounds)) * rightFaceCount +
            (GetAabbSurfaceArea(leftBounds)  / GetAabbSurfaceArea(node.bounds)) * leftFaceCount;
        if (splitCost < bestSplitCost) {
            bestSplitCost  = splitCost;
            bestSplitBin   = binIndex;
            bestLeftBounds  = leftBounds;
            bestRightBounds = rightBounds;
        }
    }

    if (bestSplitBin == 0) return false;

    // Gather mesh face IDs in bin order before overwriting their old positions.
    std::vector<unsigned int> reorderedFaceIndices;
    reorderedFaceIndices.reserve(node.faceCount);
    for (const std::vector<unsigned int>& bin : binFaceSlots) {
        for (unsigned int faceSlot : bin) {
            reorderedFaceIndices.push_back(bvh.faceIndices[faceSlot]);
        }
    }

    // Replace only this node's range in the face ordering.
    for (unsigned int i = 0; i < node.faceCount; ++i) {
        bvh.faceIndices[node.faceOffset + i] = reorderedFaceIndices[i];
    }

    // Create new bvh nodes with indices pointing to the new split
    const int leftChildIndex = static_cast<int>(bvh.nodes.size());
    const int rightChildIndex = leftChildIndex + 1;

    const unsigned int leftFaceCount = CountBinFaces(0, bestSplitBin);
    const unsigned int rightFaceOffset = node.faceOffset + leftFaceCount;
    bvh.nodes.push_back(CreateBvhNode(
        bestLeftBounds, 
        node.faceOffset, 
        leftFaceCount));
    bvh.nodes.push_back(CreateBvhNode(
        bestRightBounds,
         rightFaceOffset, 
         node.faceCount - leftFaceCount
    ));

    bvh.nodes[nodeIndex].leftChildIndex  = leftChildIndex;
    bvh.nodes[nodeIndex].rightChildIndex = rightChildIndex;

    return true;
}