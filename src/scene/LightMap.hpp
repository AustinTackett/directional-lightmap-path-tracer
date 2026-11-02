#pragma once

#include <xatlas.h>

#include "Mesh.hpp"

/*
    Light map with generated uv's for entire model for baking global illumination
*/

struct LightMap
{
    const unsigned int resolution = 0; // Single lightmap with dynamic size
    unsigned int width  = 0;
    unsigned int height = 0;
    
    // Generate texture coordinates for each mesh onto a single texture that bakes all the lighting
    bool BuildFromMeshes(std::vector<Mesh>& meshes)
    {
        if (meshes.size() == 0) {
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
        for (const Mesh& mesh : meshes)
        {
            if (mesh.vertexData.empty() || mesh.faceVertexIndices.empty()) {
                std::cerr << "Tried to build light map for mesh with either missing vertices of indices";
                return false;
            }
            xatlas::MeshDecl decl{};
            decl.vertexCount = static_cast<uint32_t>(mesh.vertexData.size());
            decl.vertexPositionData = &mesh.vertexData[0].position;
            decl.vertexPositionStride = sizeof(VertexData);
            decl.vertexNormalData = &mesh.vertexData[0].normal;
            decl.vertexNormalStride = sizeof(VertexData);
            decl.indexData = mesh.faceVertexIndices.data();
            decl.indexCount = static_cast<uint32_t>(mesh.faceVertexIndices.size() * 3);
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
        packOptions.resolution = resolution; // Use single dynamically sized texture
        packOptions.texelsPerUnit = 32.0f;
        packOptions.padding = 4;
        packOptions.bilinear = true;
        xatlas::Generate(atlas.get(), chartOptions, packOptions);
        if (atlas->atlasCount != 1 ||
            atlas->meshCount != meshes.size() ||
            atlas->width == 0 || atlas->height == 0)
        {
            std::cerr << "xatlas did not generate viable light map texture coords from the meshes";
            return false;
        }
            
        // Xatlas will not add new faces but will change the number of vertices 
        // so faceVertexIndices and vertexData must be updated.
        for (size_t i = 0; i < meshes.size(); ++i)
        {
            const xatlas::Mesh& newMesh = atlas->meshes[i];
            Mesh& oldMesh = meshes[i];

            if (newMesh.indexCount / 3 != oldMesh.faceVertexIndices.size()) {
                std::cerr << "The number of faces on the xatlas mesh did not match the original mesh data";
            }

            // Generate new vertex data
            std::vector<VertexData> newVertices(newMesh.vertexCount);
            for (uint32_t j = 0; j < newMesh.vertexCount; ++j) {
                const xatlas::Vertex& newVertex = newMesh.vertexArray[j];
                newVertices[j] = oldMesh.vertexData[newVertex.xref];
                newVertices[j].lightMapTexCoords = glm::vec3(
                    newVertex.uv[0] / atlas->width,
                    newVertex.uv[1] / atlas->height,
                    0.0f
                );
            }

            // Set face to use new vertex indices
            oldMesh.vertexData = std::move(newVertices);
            for (uint32_t j = 0; j < newMesh.indexCount / 3; ++j) {
                oldMesh.faceVertexIndices[j] = {
                    newMesh.indexArray[j * 3 + 0],
                    newMesh.indexArray[j * 3 + 1],
                    newMesh.indexArray[j * 3 + 2]
                };
            }
        }

        width  = atlas->width;
        height = atlas->height;
        return true;
    }
};

