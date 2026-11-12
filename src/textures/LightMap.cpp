#include "LightMap.hpp"
#include "Model.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include <xatlas.h>

bool LightMap::buildFromModel(Model& model, float texelsPerUnit, unsigned int padding)
{
    if (model.vertices.empty() || model.faces.empty()) {
        std::cerr << "Invalid model geometry for lightmap generation" << std::endl;
        return false;
    }

    std::cout << "Building lightmap texture coordinates from model this may take a moment..." << std::endl;

    // Create xatlas instance
    std::unique_ptr<xatlas::Atlas, decltype(&xatlas::Destroy)>
        atlas(xatlas::Create(), &xatlas::Destroy);
    if (!atlas) return false;

    // Generate Light Map Texture and Uvs
    xatlas::MeshDecl decl{};
    decl.vertexCount = static_cast<uint32_t>(model.vertices.size());
    decl.vertexPositionData = &model.vertices.front().position;
    decl.vertexPositionStride = sizeof(Vertex);
    decl.vertexNormalData = &model.vertices.front().normal;
    decl.vertexNormalStride = sizeof(Vertex);
    decl.indexData = model.faces.data();
    decl.indexCount = static_cast<uint32_t>(model.faces.size() * 3);
    decl.indexFormat = xatlas::IndexFormat::UInt32;
    const xatlas::AddMeshError error = xatlas::AddMesh(atlas.get(), decl);
    if (error != xatlas::AddMeshError::Success) {
        std::cerr << xatlas::StringForEnum(error) << std::endl;
        return false;
    }

    xatlas::PackOptions packOptions{};
    packOptions.resolution = resolution;
    packOptions.texelsPerUnit = texelsPerUnit;
    packOptions.padding = padding;
    packOptions.bilinear = true;
    xatlas::Generate(atlas.get(), xatlas::ChartOptions{}, packOptions);
    if (atlas->atlasCount != 1 || atlas->meshCount != 1 || atlas->width == 0 || atlas->height == 0) {
        std::cerr << "xatlas did not generate a single viable model lightmap" << std::endl;
        return false;
    }

    // Get Updated Mesh Data
    const xatlas::Mesh& mesh = atlas->meshes[0];
    if (mesh.indexCount != decl.indexCount) {
        std::cerr << "Number indices after light map generation unexpectedly changed" << std::endl;
        return false;
    }

    // Xatlas can alter vertex data/count, so we need check what xatlas changed
    std::vector<Vertex> vertices(mesh.vertexCount);
    const glm::vec2 atlasSize(atlas->width, atlas->height);
    for (uint32_t i = 0; i < mesh.vertexCount; ++i) {
        const xatlas::Vertex& output = mesh.vertexArray[i];
        if (output.xref >= model.vertices.size()) {
            std::cerr << "Light map references vertex out of mesh's bounds" << std::endl;
            return false;
        }
        vertices[i] = model.vertices[output.xref];
        vertices[i].lightMapTexCoords = glm::vec2(output.uv[0], output.uv[1]) / atlasSize;
    }

    // Update the vertex indices the faces reference;
    for (size_t i = 0; i < model.faces.size(); ++i) {
        Face& face = model.faces[i];
        face.vertexIndex0 = mesh.indexArray[i * 3 + 0];
        face.vertexIndex1 = mesh.indexArray[i * 3 + 1];
        face.vertexIndex2 = mesh.indexArray[i * 3 + 2];
    }

    model.vertices = std::move(vertices);
    width = atlas->width;
    height = atlas->height;

    std::cout << "Finished lightmap texture coordinates" << std::endl;
    std::cout << "Lightmap dimensions: " << width << " x " << height << std::endl;
    return true;
}