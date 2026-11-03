#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include <xatlas.h>

#include "LightMap.hpp"
#include "Scene.hpp"

// Generate texture coordinates for each mesh onto a single texture that bakes all the lighting
bool BuildFromScene(Scene& scene)
{
    if (scene.meshes.size() == 0) {
        std::cerr << "Tried to build light map with no meshes in scene" << "\n";
        return false;
    }

    // Load xatlas and make it clean up after itself
    std::unique_ptr<xatlas::Atlas, decltype(&xatlas::Destroy)> 
        atlas(xatlas::Create(), &xatlas::Destroy);
    if (!atlas) {
        std::cerr << "xatlas fail to start: " << '\n';
        return false;
    }

    // Register mesh file descriptions within xatlas
    for (const Mesh& mesh : scene.meshes)
    {
        if (mesh.vertexCount == 0 || mesh.faceCount == 0) {
            std::cerr << "Tried to build light map for mesh with either missing vertices of indices";
            return false;
        }
        xatlas::MeshDecl decl{};
        decl.vertexCount = mesh.vertexCount;
        decl.vertexPositionData = &scene.vertices[mesh.vertexOffset].position;
        decl.vertexPositionStride = sizeof(Vertex);
        decl.vertexNormalData = &scene.vertices[mesh.vertexOffset].normal;
        decl.vertexNormalStride = sizeof(Vertex);
        // xatlas expects packed triangle indices without material fields.
        std::vector<unsigned int> indices;
        indices.reserve(mesh.faceCount * 3);
        for (unsigned int i = 0; i < mesh.faceCount; ++i) {
            const Face& face = scene.faces[mesh.faceOffset + i];
            indices.push_back(face.vertexIndex0);
            indices.push_back(face.vertexIndex1);
            indices.push_back(face.vertexIndex2);
        }
        decl.indexData = indices.data();
        decl.indexCount = static_cast<uint32_t>(mesh.faceCount * 3);
        decl.indexFormat = xatlas::IndexFormat::UInt32;
        auto error = xatlas::AddMesh(atlas.get(), decl);
        if (error != xatlas::AddMeshError::Success)
        {
            std::cerr << xatlas::StringForEnum(error) << '\n';
            return false;
        }
    }

    // Generate unwrapped Uv's for light map within single texture
    xatlas::ChartOptions chartOptions{};
    xatlas::PackOptions packOptions{};
    packOptions.resolution = scene.lightMap.resolution;
    packOptions.texelsPerUnit = 32.0f;
    packOptions.padding = 4;
    packOptions.bilinear = true;
    xatlas::Generate(atlas.get(), chartOptions, packOptions);
    if (atlas->atlasCount != 1 ||
        atlas->meshCount != scene.meshes.size() ||
        atlas->width == 0 || atlas->height == 0)
    {
        std::cerr << "xatlas did not generate viable light map texture coords from the meshes";
        return false;
    }
        
    // Validate all output before changing scene ranges or faces.
    for (size_t i = 0; i < scene.meshes.size(); ++i) {
        if (atlas->meshes[i].indexCount / 3 != scene.meshes[i].faceCount) {
            std::cerr << "The number of faces on the xatlas mesh did not match the original mesh data";
            return false;
        }
    }

    // Update/add mesh vertices and uvs
    std::vector<Vertex> newVertices;
    for (size_t i = 0; i < scene.meshes.size(); ++i)
    {
        const xatlas::Mesh& newMesh = atlas->meshes[i];
        Mesh& mesh = scene.meshes[i];
        const unsigned int newOffset = static_cast<unsigned int>(newVertices.size());

        for (uint32_t j = 0; j < newMesh.vertexCount; ++j) {
            const xatlas::Vertex& newVertex = newMesh.vertexArray[j];
            Vertex vertex = scene.vertices[mesh.vertexOffset + newVertex.xref];
            vertex.lightMapTexCoords = glm::vec2(
                newVertex.uv[0] / atlas->width,
                newVertex.uv[1] / atlas->height);
            newVertices.push_back(vertex);
        }

        for (uint32_t j = 0; j < mesh.faceCount; ++j) {
            Face& face = scene.faces[mesh.faceOffset + j];
            face.vertexIndex0 = newMesh.indexArray[j * 3 + 0];
            face.vertexIndex1 = newMesh.indexArray[j * 3 + 1];
            face.vertexIndex2 = newMesh.indexArray[j * 3 + 2];
        }
        mesh.vertexOffset = newOffset;
        mesh.vertexCount = newMesh.vertexCount;
    }

    scene.vertices = std::move(newVertices);
    scene.lightMap.width  = atlas->width;
    scene.lightMap.height = atlas->height;

    return true;
}
