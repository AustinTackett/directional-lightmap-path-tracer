#pragma once

#include <glm/common.hpp>

#include "Aabb.hpp"
#include "Scene.hpp"
#include "IPrimitives.hpp"

/*
    To construct a Bvh thats points to faces on the mesh (BLAS)
*/

struct FacePrimitives final : IPrimitives
{
    const Scene& scene;
    const Mesh& mesh;

    FacePrimitives(const Scene& scene, const Mesh& mesh)
        : scene(scene), mesh(mesh) {}

    unsigned int size() override
    {
        return mesh.faceCount;
    }

    glm::vec3 getCentroid(unsigned int primitiveIndex) override
    {
        const auto& face = scene.faces[mesh.faceOffset + primitiveIndex];
        const auto& a = scene.vertices[mesh.vertexOffset + face.vertexIndex0].position;
        const auto& b = scene.vertices[mesh.vertexOffset + face.vertexIndex1].position;
        const auto& c = scene.vertices[mesh.vertexOffset + face.vertexIndex2].position;

        return (a + b + c) / 3.0f;
    }

    Aabb getAabb(unsigned int primitiveStart, unsigned int primitiveCount) override
    {
        Aabb aabb{};

        for (unsigned int offset = 0; offset < primitiveCount; ++offset) {
            const auto& face = scene.faces[mesh.faceOffset + primitiveStart + offset];
            const auto& a = scene.vertices[mesh.vertexOffset + face.vertexIndex0].position;
            const auto& b = scene.vertices[mesh.vertexOffset + face.vertexIndex1].position;
            const auto& c = scene.vertices[mesh.vertexOffset + face.vertexIndex2].position;

            aabb.expand(Aabb{
                glm::min(a, glm::min(b, c)),
                glm::max(a, glm::max(b, c))
            });
        }

        return aabb;
    }
};
