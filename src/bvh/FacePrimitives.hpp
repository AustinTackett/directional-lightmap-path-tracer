#pragma once

#include <glm/common.hpp>

#include "Aabb.hpp"
#include "IPrimitives.hpp"
#include "Model.hpp"

/*
    To construct a Bvh thats points to faces on the mesh (BLAS)
*/

struct FacePrimitives final : IPrimitives
{
    const Model& model;

    explicit FacePrimitives(const Model& model) : model(model) {}

    unsigned int size() override
    {
        return static_cast<unsigned int>(model.faces.size());
    }

    glm::vec3 getCentroid(unsigned int primitiveIndex) override
    {
        const auto& face = model.faces[primitiveIndex];
        const auto& a = model.vertices[face.vertexIndex0].position;
        const auto& b = model.vertices[face.vertexIndex1].position;
        const auto& c = model.vertices[face.vertexIndex2].position;

        return (a + b + c) / 3.0f;
    }

    Aabb getAabb(unsigned int primitiveStart, unsigned int primitiveCount) override
    {
        Aabb aabb{};

        for (unsigned int offset = 0; offset < primitiveCount; ++offset) {
            const auto& face = model.faces[primitiveStart + offset];
            const auto& a = model.vertices[face.vertexIndex0].position;
            const auto& b = model.vertices[face.vertexIndex1].position;
            const auto& c = model.vertices[face.vertexIndex2].position;

            aabb.expand(Aabb{
                glm::min(a, glm::min(b, c)),
                glm::max(a, glm::max(b, c))
            });
        }

        return aabb;
    }
};

