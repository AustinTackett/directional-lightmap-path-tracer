#pragma once

#include <stddef.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stb_image.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "Material.hpp"
#include "Textures.hpp"

/*
    Triangle Mesh Container Classes
*/

struct VertexData
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 materialTexCoords;
    glm::vec3 lightMapTexCoords; // use xatlas
};

struct FaceVertexIndices
{
    // Kept separate from face metadata to upload as EBO for rasterization
    unsigned int vertexIndex0;
    unsigned int vertexIndex1;
    unsigned int vertexIndex2;
};

struct FaceData
{
    glm::vec3    centroid;
    unsigned int materialIndex;
};

struct SubMesh
{
    // One raster draw call per submesh
    unsigned int indexStart;
    unsigned int indexCount;
    int materialIndex = -1;
};

struct Mesh
{
    std::string meshPath;
    std::unordered_map<std::string, int> texturePathToIndices;

    std::vector<VertexData>         vertexData;
    std::vector<FaceVertexIndices>  faceVertexIndices;
    std::vector<FaceData>           faceData;
    std::vector<Texture2D>          textures;
    std::vector<Material>           materials;
    std::vector<SubMesh>            subMeshes;
};

/*
    Importing 3D meshes as mesh with textures and materials
*/

bool LoadMesh(const std::string& filePath, Mesh& outMesh);
static bool GetEmbeddedTexturePath(const aiScene& sourceScene, const aiTexture* sourceTexture, std::string& outPath);
static bool LoadEmbeddedTexture(const aiTexture* sourceTexture, Texture2D& outTexture);
static bool LoadMaterialTexture(const aiMaterial& material, aiTextureType type, const aiScene& scene, Mesh& mesh, int& outTextureIndex);
static bool ProcessSubMesh(const aiMesh* sourceMesh, Mesh& mesh);
static bool ProcessMaterial(const aiMaterial& sourceMaterial, const aiScene& scene, Mesh& mesh);

// Static Objects Only, no Animation Support
bool LoadMesh(const std::string& filePath, Mesh& outMesh)
{
    std::cout << "Loading Mesh at " + filePath + "\n";

    Assimp::Importer importer;

    // Load static triangulated mesh ignoring animations
    importer.SetPropertyInteger(
        AI_CONFIG_PP_SBP_REMOVE,
        aiPrimitiveType_POINT |
        aiPrimitiveType_LINE);
    const aiScene* scene = importer.ReadFile(filePath,
        aiProcess_Triangulate |
        aiProcess_SortByPType |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals |
        aiProcess_FlipUVs |
        aiProcess_PreTransformVertices |
        aiProcess_ValidateDataStructure
    );

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        std::cerr << "Failed to load mesh " + filePath + ": " + importer.GetErrorString() << '\n';
        return false;
    }

    std::error_code pathError;
    const std::filesystem::path meshPath = std::filesystem::absolute(filePath, pathError);
    if (pathError) {
        std::cerr << "Failed to resolve mesh path " << filePath << ": " << pathError.message() << '\n';
        return false;
    }

    Mesh mesh;
    mesh.meshPath = meshPath.parent_path().string();
    mesh.textures.reserve(scene->mNumTextures);
    mesh.materials.reserve(scene->mNumMaterials);
    mesh.subMeshes.reserve(scene->mNumMeshes);

    // Add Materials and Textures to Mesh
    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        if (!ProcessMaterial(*scene->mMaterials[i], *scene, mesh)) return false;
    }

    // Add Triangle Meshes to Mesh
    auto isTriangleMesh = [](aiMesh* mesh) -> bool {
        return mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE;
    };
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        if (!isTriangleMesh(scene->mMeshes[i])) { continue; }
        if (!ProcessSubMesh(scene->mMeshes[i], mesh)) return false;
    }

    outMesh = std::move(mesh);
    return true;
}

/*
    Utilities
*/

static bool GetEmbeddedTexturePath(const aiScene& sourceScene, const aiTexture* sourceTexture, std::string& outPath)
{

    for (unsigned int i = 0; i < sourceScene.mNumTextures; i++) {
        if (sourceScene.mTextures[i] == sourceTexture) {
            outPath = "*" + std::to_string(i);
            return true;
        }
    }
    std::cerr << "Embedded texture not found in scene" << '\n';
    return false;
}

/*
    Generating Native Meshes from Assimp Information
*/

static bool LoadEmbeddedTexture(const aiTexture* sourceTexture, Texture2D& outTexture)
{
    if (!sourceTexture || !sourceTexture->pcData) {
        std::cerr << "Invalid embedded texture" << '\n';
        return false;
    }

    Texture2D texture;
    if (sourceTexture->mHeight == 0) {
        // Load image data (PNG, JPG, etc)
        int width = 0, height = 0, channels = 4;
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> data(stbi_load_from_memory(
            reinterpret_cast<const unsigned char*>(sourceTexture->pcData),
            static_cast<int>(sourceTexture->mWidth),
            &width,
            &height,
            nullptr,
            channels), stbi_image_free);
        if (!data) {
            std::cerr << "Failed to decode embedded texture" << '\n';
            return false;
        }

        size_t dataCount = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
        texture.data.assign(data.get(), data.get() + dataCount);
        texture.metaData.width = width;
        texture.metaData.height = height;
        texture.metaData.channels = channels;
        texture.metaData.format = TextureMetaData::RGBA8;
    } else {
        // Load already decoded string of RGBA data
        int width = sourceTexture->mWidth, height = sourceTexture->mHeight, channels = 4;
        texture.data.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * channels);
        for (size_t i = 0; i < texture.data.size() / 4; i++) {
            const aiTexel& pixel = sourceTexture->pcData[i];
            texture.data[i * 4 + 0] = pixel.r;
            texture.data[i * 4 + 1] = pixel.g;
            texture.data[i * 4 + 2] = pixel.b;
            texture.data[i * 4 + 3] = pixel.a;
        }
        texture.metaData.width = sourceTexture->mWidth;
        texture.metaData.height = sourceTexture->mHeight;
        texture.metaData.channels = 4;
        texture.metaData.format = TextureMetaData::RGBA8;
    }

    outTexture = std::move(texture);
    return true;
}

static bool LoadMaterialTexture(const aiMaterial& material, aiTextureType type, const aiScene& scene, Mesh& mesh, int& outTextureIndex)
{
    aiString filePath;
    if (material.GetTexture(type, 0, &filePath) != AI_SUCCESS || filePath.length == 0) {
        outTextureIndex = -1; // No texture assigned is not an import error.
        return true;
    }

    const aiTexture* embedded = scene.GetEmbeddedTexture(filePath.C_Str());
    std::string key;
    std::filesystem::path resolvedPath;
    if (embedded) {
        if (!GetEmbeddedTexturePath(scene, embedded, key)) return false;
    } else {
        if (filePath.C_Str()[0] == '*') {
            std::cerr << "Embedded texture not found: " + std::string(filePath.C_Str()) << '\n';
            return false;
        }
        resolvedPath = std::filesystem::path(filePath.C_Str());
        if (resolvedPath.is_relative()) resolvedPath = std::filesystem::path(mesh.meshPath) / resolvedPath;
        std::error_code pathError;
        resolvedPath = std::filesystem::weakly_canonical(resolvedPath, pathError);
        if (pathError) {
            std::cerr << "Failed to resolve texture path " << filePath.C_Str()
                        << ": " << pathError.message() << '\n';
            return false;
        }
        key = resolvedPath.generic_string();
    }

    const std::unordered_map<std::string, int>::const_iterator found = mesh.texturePathToIndices.find(key);
    if (found != mesh.texturePathToIndices.end()) {
        outTextureIndex = found->second;
        return true;
    }

    Texture2D texture;
    if (embedded) {
        if (!LoadEmbeddedTexture(embedded, texture)) return false;
    } else {
        if (!LoadTexture2D(resolvedPath.string(), texture)) return false;
    }
    const int textureIndex = static_cast<int>(mesh.textures.size());
    mesh.textures.push_back(std::move(texture));
    mesh.texturePathToIndices.emplace(key, textureIndex);
    outTextureIndex = textureIndex;
    return true;
}

static bool ProcessSubMesh(const aiMesh* sourceMesh, Mesh& mesh)
{
    if (!sourceMesh->HasPositions() || !sourceMesh->HasNormals()) {
        std::cerr << "Triangle mesh is missing positions or normals" << '\n';
        return false;
    }

    // Get Start of Range for Submesh
    const unsigned int vertexStart   = static_cast<unsigned int>(mesh.vertexData.size());
    const unsigned int indexStart    = static_cast<unsigned int>(mesh.faceVertexIndices.size() * 3);
    const int          materialIndex = static_cast<int>(sourceMesh->mMaterialIndex);

    // Load Vertex Data
    mesh.vertexData.reserve(mesh.vertexData.size() + sourceMesh->mNumVertices);
    mesh.faceVertexIndices.reserve(mesh.faceVertexIndices.size() + sourceMesh->mNumFaces);
    mesh.faceData.reserve(mesh.faceData.size() + sourceMesh->mNumFaces);
    const bool hasUVs = sourceMesh->HasTextureCoords(0);
    for (unsigned int i = 0; i < sourceMesh->mNumVertices; i++) {
        VertexData vertex;
        vertex.position = glm::vec3(
            sourceMesh->mVertices[i].x,
            sourceMesh->mVertices[i].y,
            sourceMesh->mVertices[i].z);
        vertex.normal = glm::vec3(
            sourceMesh->mNormals[i].x,
            sourceMesh->mNormals[i].y,
            sourceMesh->mNormals[i].z);
        vertex.materialTexCoords = glm::vec3(0.0f);
        vertex.lightMapTexCoords = glm::vec3(0.0f); // Populated when xatlas is integrated.
        if (hasUVs) {
            vertex.materialTexCoords = glm::vec3(
                sourceMesh->mTextureCoords[0][i].x,
                sourceMesh->mTextureCoords[0][i].y,
                sourceMesh->mTextureCoords[0][i].z);
        }
        mesh.vertexData.push_back(vertex);
    }

    // Load Face Indices
    for (unsigned int i = 0; i < sourceMesh->mNumFaces; i++) {
        const aiFace& face = sourceMesh->mFaces[i];
        if (face.mNumIndices != 3) {
            std::cerr << "Expected a triangle face" << '\n';
            return false;
        }
        mesh.faceVertexIndices.push_back({
            vertexStart + face.mIndices[0],
            vertexStart + face.mIndices[1],
            vertexStart + face.mIndices[2]
        });
        const FaceVertexIndices& loadedFace = mesh.faceVertexIndices.back();
        mesh.faceData.push_back({
            (mesh.vertexData[loadedFace.vertexIndex0].position +
                mesh.vertexData[loadedFace.vertexIndex1].position +
                mesh.vertexData[loadedFace.vertexIndex2].position) / 3.0f,
            sourceMesh->mMaterialIndex
        });
    }

    // Range of indices in submesh
    mesh.subMeshes.push_back(SubMesh{indexStart, sourceMesh->mNumFaces * 3, materialIndex});
    return true;
}

static bool ProcessMaterial(const aiMaterial& sourceMaterial, const aiScene& scene, Mesh& mesh)
{
    Material material;
    aiColor4D baseColor;
    if (sourceMaterial.Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS) {
        material.baseColor = glm::vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
    }
    aiColor3D emissive;
    if (sourceMaterial.Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
        material.emissive = glm::vec3(emissive.r, emissive.g, emissive.b);
    }
    sourceMaterial.Get(AI_MATKEY_METALLIC_FACTOR, material.metallic);
    sourceMaterial.Get(AI_MATKEY_ROUGHNESS_FACTOR, material.roughness);
    sourceMaterial.Get(AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_NORMALS, 0), material.normal);
    sourceMaterial.Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_LIGHTMAP, 0), material.occlusion);

    if (!LoadMaterialTexture(sourceMaterial, aiTextureType_BASE_COLOR, scene, mesh, material.baseColorTextureIndex))  {
        return false;
    }
    if (!LoadMaterialTexture(sourceMaterial, aiTextureType_GLTF_METALLIC_ROUGHNESS, scene, mesh, material.metallicRoughnessTextureIndex))  {
        return false;
    }
    if (!LoadMaterialTexture(sourceMaterial, aiTextureType_LIGHTMAP, scene, mesh, material.occlusionTextureIndex))  {
        return false;
    }
    if (!LoadMaterialTexture(sourceMaterial, aiTextureType_NORMALS, scene, mesh, material.normalTextureIndex))      {
        return false;
    }
    if (!LoadMaterialTexture(sourceMaterial, aiTextureType_EMISSIVE, scene, mesh, material.emissiveTextureIndex))   {
        return false;
    }

    // Other names occlusion texture could be under
    if (material.occlusionTextureIndex == -1) {
        if(!LoadMaterialTexture(sourceMaterial, aiTextureType_AMBIENT_OCCLUSION, scene, mesh, material.occlusionTextureIndex))
            return false;
    }
    
    mesh.materials.push_back(std::move(material));
    return true;
}
