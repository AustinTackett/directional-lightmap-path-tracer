#pragma once 

#include <algorithm>
#include <array>
#include <iostream>
#include <stack>
#include <vector>

#include <glm/common.hpp>
#include <glm/vec3.hpp>

#include "IPrimitives.hpp"
#include "Aabb.hpp"
#include "AabbBins.hpp"

/*
    Construct a tree for fast ray querying of any object that implements IPrimitive

    Bvh Data (Nodes aligned for std430 SSBO's)
    -> std430 
        1. float/uint/int/bool 4 bytes
        2. vec2 8 bytes
        3. vec3/vec4 16 bytes
*/

constexpr float TRAVERSAL_COST = 1.0f;
constexpr float INTERSECTION_COST = 1.0f;
constexpr unsigned int BINS_PER_SPLIT = 128;
constexpr unsigned int MODEL_FACES_BVH_LEAF = 4;

struct alignas(16) BvhNode
{
    alignas(16) Aabb aabb;

    alignas(4) unsigned int bvhIndexStart;
    alignas(4) unsigned int bvhIndexCount;

    alignas(4) int leftNodeIndex  = -1;
    alignas(4) int rightNodeIndex = -1;

    BvhNode(const Aabb& bounds, unsigned int start, unsigned int count)
        : aabb(bounds), bvhIndexStart(start), bvhIndexCount(count)
    {}
};

// Aabb is a nested GLSL struct, so its trailing padding is part of the layout.
static_assert(sizeof(BvhNode) == 48 && alignof(BvhNode) == 16);
static_assert(offsetof(BvhNode, aabb) == 0 && offsetof(BvhNode, bvhIndexStart) == 32);
static_assert(offsetof(BvhNode, bvhIndexCount) == 36);
static_assert(offsetof(BvhNode, leftNodeIndex) == 40 && offsetof(BvhNode, rightNodeIndex) == 44);

class Bvh
{
    public: 
    
    std::vector<BvhNode>      nodes;
    std::vector<unsigned int> primitiveIndices;

    template<unsigned int MAX_LEAF_PRIMITIVES>
    bool BuildBvh(IPrimitives& primitives) {
        // Check Mesh if Valid
        std::cout << "Building Bvh" << std::endl;
        const unsigned int bvhIndexCount = primitives.size();
        if (bvhIndexCount == 0) {
            std::cout << "No primitives to construct BVH" << std::endl;
            return false;
        }

        // Initialize bvh indices
        nodes.clear();
        primitiveIndices.clear();
        for (unsigned int i = 0; i < bvhIndexCount; ++i) {
            primitiveIndices.push_back(i);
        }

        // Create Root Node
        nodes.emplace_back(primitives.getAabb(0, bvhIndexCount), 0, bvhIndexCount);

        // Construct Tree Top Down from Root
        std::stack<unsigned int> frontierNodeIndices;
        frontierNodeIndices.push(0);
        while (!frontierNodeIndices.empty()) {
            const unsigned int nodeIndex = frontierNodeIndices.top();
            frontierNodeIndices.pop();

            // Create child nodes until you reach terminal condition
            if (SplitBvhNode<MAX_LEAF_PRIMITIVES>(primitives, nodeIndex)) {
                frontierNodeIndices.push(nodes[nodeIndex].rightNodeIndex);
                frontierNodeIndices.push(nodes[nodeIndex].leftNodeIndex);
            }
        }

        std::cout << "Bvh built with " << nodes.size() << " nodes" << std::endl;
        return true;
    }

    private:

    template<unsigned int MAX_LEAF_PRIMITIVES>
    bool SplitBvhNode(IPrimitives& primitives, unsigned int nodeIndex)
    {
        const BvhNode node = nodes[nodeIndex];

        if (node.bvhIndexCount <= MAX_LEAF_PRIMITIVES)
            return false;

        // Find centroid bounds for this node
        glm::vec3 centroidMin = primitives.getAabb(primitiveIndices[node.bvhIndexStart], 1).getCentroid();
        glm::vec3 centroidMax = centroidMin;
        for (unsigned int slot = node.bvhIndexStart + 1; slot < node.bvhIndexStart + node.bvhIndexCount; ++slot) {
            unsigned int primitiveIndex = primitiveIndices[slot];
            glm::vec3 centroid = primitives.getAabb(primitiveIndex, 1).getCentroid();
            centroidMin = glm::min(centroidMin, centroid);
            centroidMax = glm::max(centroidMax, centroid);
        }
        
        const Aabb& nodeAabb = nodes[nodeIndex].aabb;

        // Evaluate candidate SAH splits
        float parentArea = nodeAabb.getSurfaceArea();
        if (parentArea <= 0.0f) {
            return false;
        }

        float bestSplitCost = static_cast<float>(node.bvhIndexCount);
        unsigned int bestSplitBin = 0;
        unsigned int bestSplitAxis = 0;
        Aabb bestLeftBounds{};
        Aabb bestRightBounds{};
        for (unsigned int splitAxis = 0; splitAxis < 3; ++splitAxis)
        {
            AabbBins<BINS_PER_SPLIT> bins(centroidMin, centroidMax, splitAxis);
            for (unsigned int slot = node.bvhIndexStart; slot < node.bvhIndexStart + node.bvhIndexCount; ++slot)
            {
                unsigned int primitiveIndex = primitiveIndices[slot];
                bins.add(primitives.getAabb(primitiveIndex, 1));
            }

            for (unsigned int splitBin = 1; splitBin < BINS_PER_SPLIT; ++splitBin)
            {
                auto [leftBounds, leftCount]   = bins.CombineBins(0, splitBin);
                auto [rightBounds, rightCount] = bins.CombineBins(splitBin, BINS_PER_SPLIT);
                if (leftCount == 0 || rightCount == 0)
                    continue;

                float cost = TRAVERSAL_COST 
                    + INTERSECTION_COST 
                    * (leftBounds.getSurfaceArea() / parentArea * leftCount + rightBounds.getSurfaceArea() / parentArea * rightCount);
                if (cost < bestSplitCost)
                {
                    bestSplitCost   = cost;
                    bestSplitBin    = splitBin;
                    bestSplitAxis   = splitAxis;
                    bestLeftBounds  = leftBounds;
                    bestRightBounds = rightBounds;
                }
            }
        }
        if (bestSplitBin == 0)
            return false;

        // Sort bvh cells into left and right of choosen plit
        AabbBins<BINS_PER_SPLIT> bins(centroidMin, centroidMax, bestSplitAxis);
        auto isLeftOfSplit = [&](unsigned int primitiveIndex) {
            return bins.findBinIndex(primitives.getAabb(primitiveIndex, 1)) < bestSplitBin;
        };
        auto begin  = primitiveIndices.begin() + node.bvhIndexStart;
        auto end    = begin + node.bvhIndexCount;
        auto middle = std::partition(begin, end, isLeftOfSplit);
        unsigned int leftCount = static_cast<unsigned int>(middle - begin);
        if (leftCount == 0 || leftCount == node.bvhIndexCount)
            return false;

        // Create child nodes
        const int leftNodeIndex = static_cast<int>(nodes.size());
        const int rightNodeIndex = leftNodeIndex + 1;
        nodes.emplace_back(bestLeftBounds, node.bvhIndexStart, leftCount);
        nodes.emplace_back(bestRightBounds, node.bvhIndexStart + leftCount, node.bvhIndexCount - leftCount);

        nodes[nodeIndex].leftNodeIndex  = leftNodeIndex;
        nodes[nodeIndex].rightNodeIndex = rightNodeIndex;

        return true;
    }
};
