#pragma once

#include <cassert>

#include "Bvh.hpp"
#include "IPrimitives.hpp"
#include "Scene.hpp"

/*
    To construct a Bvh thats points to other bvhs (TLAS)
*/

struct BvhPrimitives final : IPrimitives
{
    // Bounds and primitive indices retain the input mesh order.
    std::vector<Aabb> bounds;

    explicit BvhPrimitives(const Scene& scene)
    {
        bounds.reserve(scene.meshes.size());
        for (const Mesh& mesh : scene.meshes) {
            Aabb aabb = scene.blasNodes[mesh.blasNodeOffset].aabb;
            aabb.applyMatrix(mesh.localToWorld);
            bounds.push_back(aabb);
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
