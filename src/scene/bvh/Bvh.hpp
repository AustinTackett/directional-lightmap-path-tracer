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

constexpr unsigned int TARGET_LEAF_FACE_COUNT = 32;
constexpr unsigned int BINS_PER_SPLIT         = 16;

struct alignas(16) BvhNode
{
    Aabb aabb;

    // face indices in bvh class not mesh
    unsigned int primitiveStart;
    unsigned int primitiveCount;

    int leftChildIndex  = -1;
    int rightChildIndex = -1;

    BvhNode(const Aabb& bounds, unsigned int start, unsigned int count)
        : aabb(bounds), primitiveStart(start), primitiveCount(count)
    {}
};

struct Bvh
{
    std::vector<BvhNode>      nodes;
    std::vector<unsigned int> primitiveIndices;

    bool BuildBvh(IPrimitives& primitives) {
        // Check Mesh if Valid
        std::cout << "Building Bvh" << "\n";
        const unsigned int primitiveCount = primitives.size();
        if (primitiveCount == 0) {
            std::cout << "No primitives to construct BVH" << "\n";
            return false;
        }

        // Initialize bvh indices
        nodes.clear();
        primitiveIndices.clear();
        for (unsigned int i = 0; i < primitiveCount; ++i) {
            primitiveIndices.push_back(i);
        }

        // Create Root Node
        nodes.emplace_back(primitives.getAabb(0, primitiveCount), 0, primitiveCount);

        // Construct Tree Top Down from Root
        std::stack<unsigned int> frontierNodeIndices;
        frontierNodeIndices.push(0);
        while (!frontierNodeIndices.empty()) {
            const unsigned int nodeIndex = frontierNodeIndices.top();
            frontierNodeIndices.pop();

            // Create child nodes until you reach terminal condition
            if (SplitBvhNode(primitives, nodeIndex)) {
                frontierNodeIndices.push(nodes[nodeIndex].rightChildIndex);
                frontierNodeIndices.push(nodes[nodeIndex].leftChildIndex);
            }
        }

        std::cout << "Bvh built with " << nodes.size() << " nodes" << "\n";
        return true;
    }

    bool SplitBvhNode(IPrimitives& primitives, unsigned int nodeIndex)
    {
        const BvhNode node = nodes[nodeIndex];

        if (node.primitiveCount <= TARGET_LEAF_FACE_COUNT)
            return false;

        // Find centroid bounds for this node
        glm::vec3 centroidMin = primitives.getAabb(primitiveIndices[node.primitiveStart], 1).getCentroid();
        glm::vec3 centroidMax = centroidMin;
        for (unsigned int slot = node.primitiveStart + 1; slot < node.primitiveStart + node.primitiveCount; ++slot) {
            unsigned int primitiveIndex = primitiveIndices[slot];
            glm::vec3 centroid = primitives.getAabb(primitiveIndex, 1).getCentroid();
            centroidMin = glm::min(centroidMin, centroid);
            centroidMax = glm::max(centroidMax, centroid);
        }
        
        // Bin Aabbs along specified axis
        const Aabb& nodeAabb = nodes[nodeIndex].aabb;
        unsigned int splitAxis = nodes[nodeIndex].aabb.getLongestAxis();
        AabbBins<BINS_PER_SPLIT> bins(centroidMin, centroidMax, splitAxis);
        for (unsigned int slot = node.primitiveStart; slot < node.primitiveStart + node.primitiveCount; ++slot) 
        {
            unsigned int primitiveIndex = primitiveIndices[slot];
            bins.add(primitives.getAabb(primitiveIndex, 1));
        }

        // Evaluate candidate SAH splits
        float parentArea = nodeAabb.getSurfaceArea();
        if (parentArea <= 0.0f) {
            return false;
        }

        float bestSplitCost = static_cast<float>(node.primitiveCount);
        unsigned int bestSplitBin = 0;
        Aabb bestLeftBounds{};
        Aabb bestRightBounds{};
        for (unsigned int splitBin = 1; splitBin < BINS_PER_SPLIT; ++splitBin)
        {
            auto [leftBounds, leftCount] = bins.CombineBins(0, splitBin);
            auto [rightBounds, rightCount] = bins.CombineBins(splitBin, BINS_PER_SPLIT);
            if (leftCount == 0 || rightCount == 0)
                continue;
            float cost = 1.0f +
                leftBounds.getSurfaceArea() / parentArea * leftCount +
                rightBounds.getSurfaceArea() / parentArea * rightCount;
            if (cost < bestSplitCost)
            {
                bestSplitCost = cost;
                bestSplitBin = splitBin;
                bestLeftBounds = leftBounds;
                bestRightBounds = rightBounds;
            }
        }
        if (bestSplitBin == 0)
            return false;

        // Reorder only the BVH's primitive indices
        auto isLeftOfSplit = [&](unsigned int primitiveIndex) {
            return bins.findBinIndex(primitives.getAabb(primitiveIndex, 1)) < bestSplitBin;
        };
        auto begin = primitiveIndices.begin() + node.primitiveStart;
        auto end   = begin + node.primitiveCount;
        auto middle = std::partition(begin, end, isLeftOfSplit);
        unsigned int leftCount = static_cast<unsigned int>(middle - begin);
        if (leftCount == 0 || leftCount == node.primitiveCount)
            return false;

        // Create child nodes
        int leftChildIndex = static_cast<int>(nodes.size());
        int rightChildIndex = leftChildIndex + 1;
        nodes.emplace_back(bestLeftBounds, node.primitiveStart, leftCount);
        nodes.emplace_back(bestRightBounds, node.primitiveStart + leftCount, node.primitiveCount - leftCount);
        nodes[nodeIndex].leftChildIndex = leftChildIndex;
        nodes[nodeIndex].rightChildIndex = rightChildIndex;

        return true;
    }
};
