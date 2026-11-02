#pragma once

#include <cassert>

#include "Bvh.hpp"
#include "IPrimitives.hpp"
#include "../Transform.hpp"

/*
    To construct a Bvh thats points to other bvhs (TLAS)
*/

struct BvhPrimitives final : IPrimitives
{
    // Bounds and primitive indices retain the input mesh order.
    std::vector<Aabb> bounds;

    BvhPrimitives(const std::vector<Bvh>& bvhs, const std::vector<Transform>& transforms)
    {
        bounds.reserve(bvhs.size());
        for (size_t i = 0; i < bvhs.size(); ++i) {
            Aabb world = bvhs[i].nodes.front().aabb;
            world.applyMatrix(transforms[i].getModelTransform());
            bounds.push_back(world);
        }
    }

    unsigned int size() override
    {
        return static_cast<unsigned int>(bounds.size());
    }

    glm::vec3 getCentroid(unsigned int primitiveIndex) override
    {
        return bounds[primitiveIndex].getCentroid();
    }

    Aabb getAabb(unsigned int primitiveStart, unsigned int primitiveCount) override
    {
        Aabb combined;
        for (unsigned int offset = 0; offset < primitiveCount; ++offset)
            combined.expand(bounds[primitiveStart + offset]);
        return combined;
    }
};
