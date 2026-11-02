#pragma once

#include <glm/common.hpp>

#include "Aabb.hpp"
#include "Mesh.hpp"
#include "IPrimitives.hpp"

/*
    To construct a Bvh thats points to faces on the mesh (BLAS)
*/

struct FacePrimitives final : IPrimitives
{
    const Mesh& mesh;

    explicit FacePrimitives(const Mesh& mesh) : mesh(mesh) {}

    unsigned int size() override
    {
        return static_cast<unsigned int>(mesh.faceVertexIndices.size());
    }

    glm::vec3 getCentroid(unsigned int primitiveIndex) override
    {
        const auto& face = mesh.faceVertexIndices[primitiveIndex];
        const auto& a = mesh.vertexData[face.vertexIndex0].position;
        const auto& b = mesh.vertexData[face.vertexIndex1].position;
        const auto& c = mesh.vertexData[face.vertexIndex2].position;

        return (a + b + c) / 3.0f;
    }

    Aabb getAabb(unsigned int primitiveStart, unsigned int primitiveCount) override
    {
        Aabb bounds{};

        for (unsigned int offset = 0; offset < primitiveCount; ++offset) {
            const auto& face = mesh.faceVertexIndices[primitiveStart + offset];
            const auto& a = mesh.vertexData[face.vertexIndex0].position;
            const auto& b = mesh.vertexData[face.vertexIndex1].position;
            const auto& c = mesh.vertexData[face.vertexIndex2].position;

            bounds.expand(Aabb{
                glm::min(a, glm::min(b, c)),
                glm::max(a, glm::max(b, c))
            });
        }

        return bounds;
    }
};
