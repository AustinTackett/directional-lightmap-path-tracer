#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <tuple>

#include "Aabb.hpp"

/*
    Create histogram of Aabbs's binned by a specified axis
    Allows a Aabbs to be combined and split into pairs by computing finite splits
    -> Utility for surface area heuristic construction of bvh's
*/

template<unsigned int BINS>
struct AabbBins
{
    static_assert(BINS > 0, "A histogram must have at least one bin");
    
    std::array<unsigned int, BINS> counts{};
    std::array<Aabb, BINS>         aabbs{};
    const glm::vec3 min;
    const glm::vec3 max;
    const unsigned int splitAxis;

    AabbBins(glm::vec3 minValue, glm::vec3 maxValue, unsigned int axis = 0): min(minValue), max(maxValue), splitAxis(axis)
    {
    }

    std::tuple<Aabb, Aabb> getAabbSplit(unsigned int splitBin) const
    {
        return {std::get<0>(CombineBins(0, splitBin)), std::get<0>(CombineBins(splitBin, BINS))};
    }

    void add(const Aabb& aabb)
    {
        unsigned int binIndex = findBinIndex(aabb);
        if (counts[binIndex] == 0)
            aabbs[binIndex] = aabb;
        else
            aabbs[binIndex].expand(aabb);

        counts[binIndex]++;
    }

    unsigned int findBinIndex(const Aabb& aabb) const
    {
        float center = aabb.getCentroid()[splitAxis];
        float extent = max[splitAxis] - min[splitAxis];
        if (extent <= 0.0f)
            return 0;
        float normalized = std::clamp((center - min[splitAxis]) / extent, 0.0f, 1.0f);
        return std::min(static_cast<unsigned int>(normalized * BINS), BINS - 1);
    }

    // Combine aabbs and counts over the range [start, end).
    std::tuple<Aabb, unsigned int> CombineBins(unsigned int binStart, unsigned int binEnd) const
    {
        Aabb combined;
        unsigned int count = 0;
        for (unsigned int i = binStart; i < binEnd; ++i) {
            if (counts[i] == 0)
                continue;
            combined.expand(aabbs[i]);
            count += counts[i];
        }
        return {combined, count};
    }
};
