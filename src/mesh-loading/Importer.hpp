#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_map>

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stb_image.h>

#include "TriMesh.hpp"
#include "Textures.hpp"
#include "assimp/mesh.h"

class Importer
{
    private: 

    /*
        Helper Class to Track Previously Loaded Data
    */

    struct TextureIndexCache
    {
        std::string modelPath;
        std::unordered_map<std::string, int> files;

        void insert(const std::string& filePath, int textureIndex)
        {
            files.emplace(filePath, textureIndex);
        }

        int get(const std::string& filePath)
        {
            auto found = files.find(filePath);
            if (found != files.end()) { return found->second; }
            else { return -1; }
        }
    };

    public:

    /*
        Public Loaders to Create Native Engine Objects from Filepaths
    */

    // Static Objects Only, no Animation Support
    static void LoadMesh(const std::string& filePath, Mesh& outModel)
    {
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
            throw std::runtime_error("Failed to load model " + filePath + ": " + importer.GetErrorString());
        }

        Mesh model;
        model.textures.reserve(scene->mNumTextures);
        model.materials.reserve(scene->mNumMaterials);
        model.subMeshes.reserve(scene->mNumMeshes);

        // Add Materials and Textures to Model
        TextureIndexCache textureCache{filePath};
        for (unsigned int i = 0; i < scene->mNumTextures; i++) {
            ProcessEmbeddedTexture(scene->mTextures[i], *scene, model, textureCache);
        }
        for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
            ProcessMaterial(scene->mMaterials[i], *scene, model, textureCache);
        }
        
        // Add Triangle Meshes to Model
        auto isTriangleMesh = [](aiMesh* mesh) -> bool { 
            return mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE; 
        };
        for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
            if (!isTriangleMesh(scene->mMeshes[i])) { continue; } 
            ProcessSubMesh(scene->mMeshes[i], model);
        }

        // Calculate bvh

        outModel = std::move(model);
    }

    static void LoadTexture2D(const std::string& filePath, Texture2D& outTexture)
    {
        int width = 0, height = 0, channels = 4;
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> data(stbi_load(
            filePath.c_str(),
            &width,
            &height,
            nullptr,
            channels
        ), stbi_image_free);

        if (!data) {
            throw std::runtime_error("Failed to load texture: " + filePath);
        }

        Texture2D texture;
        size_t dataCount = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);        
        texture.data.assign(data.get(), data.get() + dataCount);
        texture.metaData.width = width;
        texture.metaData.height = height;
        texture.metaData.channels = channels;
        texture.metaData.format = TextureMetaData::RGBA8;

        outTexture = std::move(texture);
    }

    private:

    /*
        Utilities
    */

    static std::string GetEmbeddedTexturePath(const aiScene& sourceScene, const aiTexture* sourceTexture)
    {
        if (!sourceTexture) { return {}; }
        for (unsigned int i = 0; i < sourceScene.mNumTextures; i++) {
            if (sourceScene.mTextures[i] == sourceTexture) {
                return "*" + std::to_string(i);
            }
        }
        throw std::runtime_error("Embedded texture not found in scene");
    }

    /*
        Generating Native Models from Assimp Information
    */

    static void ProcessSubMesh(const aiMesh* sourceMesh, Mesh& model)
    {
        if (!sourceMesh->HasPositions() || !sourceMesh->HasNormals()) {
            throw std::runtime_error("Triangle mesh is missing positions or normals");
        }

        // Get Start of Range for Submesh
        const unsigned int vertexStart   = static_cast<unsigned int>(model.vertices.size());
        const unsigned int indexStart    = static_cast<unsigned int>(model.indices.size());
        const int          materialIndex = static_cast<int>(sourceMesh->mMaterialIndex);

        // Load Vertex Data
        model.vertices.reserve(model.vertices.size() + sourceMesh->mNumVertices);
        model.faces.reserve(model.faces.size() + sourceMesh->mNumFaces);
        model.indices.reserve(model.indices.size() + static_cast<size_t>(sourceMesh->mNumFaces) * 3);
        const bool hasUVs = sourceMesh->HasTextureCoords(0);
        for (unsigned int i = 0; i < sourceMesh->mNumVertices; i++) {
            Vertex vertex;
            vertex.position = glm::vec3(
                sourceMesh->mVertices[i].x,
                sourceMesh->mVertices[i].y,
                sourceMesh->mVertices[i].z);
            vertex.normal = glm::vec3(
                sourceMesh->mNormals[i].x,
                sourceMesh->mNormals[i].y,
                sourceMesh->mNormals[i].z);
            vertex.texCoords = glm::vec3(0.0f);
            if (hasUVs) {
                vertex.texCoords = glm::vec3(
                    sourceMesh->mTextureCoords[0][i].x,
                    sourceMesh->mTextureCoords[0][i].y,
                    sourceMesh->mTextureCoords[0][i].z);
            }
            model.vertices.push_back(vertex);
        }

        // Load Face Indices
        for (unsigned int i = 0; i < sourceMesh->mNumFaces; i++) {
            const aiFace& face = sourceMesh->mFaces[i];
            if (face.mNumIndices != 3) { 
                throw std::runtime_error("Expected a triangle face"); 
            }
            model.faces.push_back({
                vertexStart + face.mIndices[0],
                vertexStart + face.mIndices[1],
                vertexStart + face.mIndices[2],
                materialIndex
            });
            const Face& loadedFace = model.faces.back();
            model.indices.insert(model.indices.end(), {
                loadedFace.index0, loadedFace.index1, loadedFace.index2
            });
        }

        // Range of indices in submesh
        model.subMeshes.push_back(SubMesh{indexStart, sourceMesh->mNumFaces * 3, materialIndex});
    }

    static void ProcessEmbeddedTexture(const aiTexture* sourceTexture, const aiScene& sourceScene, Mesh& model, TextureIndexCache& cache)
    {
        if (!sourceTexture || !sourceTexture->pcData) { 
            throw std::runtime_error("Invalid embedded texture"); 
        }
        std::string key = GetEmbeddedTexturePath(sourceScene, sourceTexture);
        
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
                throw std::runtime_error("Failed to decode embedded texture");
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

        const int textureIndex = static_cast<int>(model.textures.size());
        model.textures.push_back(std::move(texture));
        cache.insert(key, textureIndex);
    }

    static void ProcessMaterial(const aiMaterial* sourceMaterial, const aiScene& sourceScene, Mesh& model, TextureIndexCache& cache)
    {
        auto loadMaterialTexture = [&](aiTextureType type) -> int {

            // Get texture file path if it exists
            std::string textureFilePath;
            aiString    aiFilePath;
            if (sourceMaterial->GetTexture(type, 0, &aiFilePath) == AI_SUCCESS) {
                textureFilePath = std::string(aiFilePath.C_Str());
            }
            if (textureFilePath.empty()) {
                return -1;
            }
            
            // Check if texture is embedded
            std::string embeddedPath = GetEmbeddedTexturePath(sourceScene, sourceScene.GetEmbeddedTexture(textureFilePath.c_str()));
            if (!embeddedPath.empty()) {
                const int textureIndex = cache.get(embeddedPath);
                if (textureIndex < 0) { throw std::runtime_error("Embedded texture was not loaded before material was processed: " + textureFilePath); }
                return textureIndex;
            }
            if (textureFilePath[0] == '*') { throw std::runtime_error("External texture has embedded texture file path " + textureFilePath); }

            // Resolve file path for non-embeddd texture
            std::filesystem::path resolvedTexturePath = std::filesystem::path(textureFilePath);
            if (resolvedTexturePath.is_relative()) {
                resolvedTexturePath = std::filesystem::path(cache.modelPath).parent_path() / resolvedTexturePath;
            } else {
                resolvedTexturePath = std::filesystem::weakly_canonical(
                    std::filesystem::absolute(resolvedTexturePath)).generic_string();
            }

            // If it is already loaded don't reload
            int textureIndex = cache.get(resolvedTexturePath.string());
            if (textureIndex >= 0) { return textureIndex; }

            // Standard non-embedded texture data load
            Texture2D texture;
            LoadTexture2D(resolvedTexturePath.string(), texture);
            textureIndex = static_cast<int>(model.textures.size());
            model.textures.push_back(std::move(texture));
            cache.insert(resolvedTexturePath.string(), textureIndex);
            return textureIndex;
        };

        // Legacy Material Properties
        Material material;
        aiColor3D color;
        float value;
        if (sourceMaterial->Get(AI_MATKEY_COLOR_AMBIENT, color) == AI_SUCCESS) {
            material.Ka = glm::vec3(color.r, color.g, color.b);
        }
        if (sourceMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
            material.Kd = glm::vec3(color.r, color.g, color.b);
        }
        if (sourceMaterial->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS) {
            material.Ks = glm::vec3(color.r, color.g, color.b);
        }
        if (sourceMaterial->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS) {
            material.Ke = glm::vec3(color.r, color.g, color.b);
        }
        if (sourceMaterial->Get(AI_MATKEY_COLOR_TRANSPARENT, color) == AI_SUCCESS) {
            material.Tf = glm::vec3(color.r, color.g, color.b);
        }
        if (sourceMaterial->Get(AI_MATKEY_SHININESS, value) == AI_SUCCESS) {
            material.Ns = value;
        }
        if (sourceMaterial->Get(AI_MATKEY_REFRACTI, value) == AI_SUCCESS) {
            material.Ni = value;
        }
        if (sourceMaterial->Get(AI_MATKEY_OPACITY, value) == AI_SUCCESS) {
            material.d = value;
        }

        // PBR Material Properties
        aiColor4D baseColor;
        if (sourceMaterial->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS) {
            material.Kd = glm::vec3(baseColor.r, baseColor.g, baseColor.b);
            material.d = baseColor.a;
        }
        if (sourceMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, value) == AI_SUCCESS) {
            material.Pr = value;
        }
        if (sourceMaterial->Get(AI_MATKEY_METALLIC_FACTOR, value) == AI_SUCCESS) {
            material.Pm = value;
        }

        // Legacy Texture Maps
        material.map_Kd_index   = loadMaterialTexture(aiTextureType_BASE_COLOR);
        material.map_Ka_index   = loadMaterialTexture(aiTextureType_AMBIENT);
        material.map_Ks_index   = loadMaterialTexture(aiTextureType_SPECULAR);
        material.map_Ns_index   = loadMaterialTexture(aiTextureType_SHININESS);
        material.map_d_index    = loadMaterialTexture(aiTextureType_OPACITY);

        // PBR Texture Maps
        material.map_Pr_index   = loadMaterialTexture(aiTextureType_DIFFUSE_ROUGHNESS);
        material.map_Pm_index   = loadMaterialTexture(aiTextureType_METALNESS);
        material.map_Ke_index   = loadMaterialTexture(aiTextureType_EMISSION_COLOR);
        material.map_norm_index = loadMaterialTexture(aiTextureType_NORMALS);

        // Fall back to legacy texture names
        if (material.map_Kd_index == -1) {
            material.map_Kd_index = loadMaterialTexture(aiTextureType_DIFFUSE);
        }
        if (material.map_Ke_index == -1) {
            material.map_Ke_index = loadMaterialTexture(aiTextureType_EMISSIVE);
        }

        model.materials.push_back(std::move(material));
    }

};



