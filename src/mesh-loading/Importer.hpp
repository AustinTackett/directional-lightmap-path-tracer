#pragma once

#include <filesystem>
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

#include "Mesh.hpp"
#include "Textures.hpp"
#include "assimp/mesh.h"

class Importer
{

/*
    Importing 3D models as mesh with textures and materials 
    -> I'm tired... :(
*/

private: 

    struct MeshImportContext
    {
        Mesh mesh;
        const aiScene& scene;
        std::filesystem::path directory; 
        std::unordered_map<std::string, int> textureIndices;
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

        MeshImportContext context{
            outModel, 
            *scene, 
            std::filesystem::absolute(filePath).parent_path(), 
            {}};
        Mesh& model = context.mesh;
        model.textures.reserve(scene->mNumTextures);
        model.materials.reserve(scene->mNumMaterials);
        model.subMeshes.reserve(scene->mNumMeshes);

        // Add Materials and Textures to Model
        for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
            ProcessMaterial(*scene->mMaterials[i], context);
        }
        
        // Add Triangle Meshes to Model
        auto isTriangleMesh = [](aiMesh* mesh) -> bool { 
            return mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE; 
        };
        for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
            if (!isTriangleMesh(scene->mMeshes[i])) { continue; } 
            ProcessSubMesh(scene->mMeshes[i], model);
        }

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
        texture.metaData.width    = width;
        texture.metaData.height   = height;
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

    static Texture2D LoadEmbeddedTexture(const aiTexture* sourceTexture)
    {
        if (!sourceTexture || !sourceTexture->pcData) { 
            throw std::runtime_error("Invalid embedded texture"); 
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

        return texture;
    }

    static int LoadMaterialTexture(const aiMaterial& material, aiTextureType type, MeshImportContext& context)
    {
        aiString filePath;
        if (material.GetTexture(type, 0, &filePath) != AI_SUCCESS || filePath.length == 0) {
            return -1;
        }

        const aiTexture* embedded = context.scene.GetEmbeddedTexture(filePath.C_Str());
        std::string key;
        std::filesystem::path resolvedPath;
        if (embedded) {
            key = GetEmbeddedTexturePath(context.scene, embedded);
        } else {
            if (filePath.C_Str()[0] == '*') {
                throw std::runtime_error("Embedded texture not found: " + std::string(filePath.C_Str()));
            }
            resolvedPath = std::filesystem::path(filePath.C_Str());
            if (resolvedPath.is_relative()) resolvedPath = context.directory / resolvedPath;
            resolvedPath = std::filesystem::weakly_canonical(std::filesystem::absolute(resolvedPath));
            key = resolvedPath.generic_string();
        }

        const std::unordered_map<std::string, int>::const_iterator found = context.textureIndices.find(key);
        if (found != context.textureIndices.end()) return found->second;

        Texture2D texture;
        if (embedded) {
            texture = LoadEmbeddedTexture(embedded);
        } else {
            LoadTexture2D(resolvedPath.string(), texture);
        }
        const int textureIndex = static_cast<int>(context.mesh.textures.size());
        context.mesh.textures.push_back(std::move(texture));
        context.textureIndices.emplace(key, textureIndex);
        return textureIndex;
    }

    static void ProcessSubMesh(const aiMesh* sourceMesh, Mesh& model)
    {
        if (!sourceMesh->HasPositions() || !sourceMesh->HasNormals()) {
            throw std::runtime_error("Triangle mesh is missing positions or normals");
        }

        // Get Start of Range for Submesh
        const unsigned int vertexStart   = static_cast<unsigned int>(model.vertices.size());
        const unsigned int indexStart    = static_cast<unsigned int>(model.faceVertexIndices.size() * 3);
        const int          materialIndex = static_cast<int>(sourceMesh->mMaterialIndex);

        // Load Vertex Data
        model.vertices.reserve(model.vertices.size() + sourceMesh->mNumVertices);
        model.faceVertexIndices.reserve(model.faceVertexIndices.size() + sourceMesh->mNumFaces);
        model.faceData.reserve(model.faceData.size() + sourceMesh->mNumFaces);
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
            model.vertices.push_back(vertex);
        }

        // Load Face Indices
        for (unsigned int i = 0; i < sourceMesh->mNumFaces; i++) {
            const aiFace& face = sourceMesh->mFaces[i];
            if (face.mNumIndices != 3) { 
                throw std::runtime_error("Expected a triangle face"); 
            }
            model.faceVertexIndices.push_back({
                vertexStart + face.mIndices[0],
                vertexStart + face.mIndices[1],
                vertexStart + face.mIndices[2]
            });
            const FaceVertexIndices& loadedFace = model.faceVertexIndices.back();
            model.faceData.push_back({
                (model.vertices[loadedFace.vertexIndex0].position +
                 model.vertices[loadedFace.vertexIndex1].position +
                 model.vertices[loadedFace.vertexIndex2].position) / 3.0f,
                sourceMesh->mMaterialIndex
            });
        }

        // Range of indices in submesh
        model.subMeshes.push_back(SubMesh{indexStart, sourceMesh->mNumFaces * 3, materialIndex});
    }

    static void ProcessMaterial(const aiMaterial& sourceMaterial, MeshImportContext& context)
    {
        // Legacy Material Properties
        Material material;
        aiColor3D color;
        float value;
        if (sourceMaterial.Get(AI_MATKEY_COLOR_AMBIENT, color) == AI_SUCCESS) {
            material.Ka = glm::vec3(color.r, color.g, color.b);
        }
        if (sourceMaterial.Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
            material.Kd = glm::vec3(color.r, color.g, color.b);
        }
        if (sourceMaterial.Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS) {
            material.Ks = glm::vec3(color.r, color.g, color.b);
        }
        if (sourceMaterial.Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS) {
            material.Ke = glm::vec3(color.r, color.g, color.b);
        }
        if (sourceMaterial.Get(AI_MATKEY_COLOR_TRANSPARENT, color) == AI_SUCCESS) {
            material.Tf = glm::vec3(color.r, color.g, color.b);
        }
        if (sourceMaterial.Get(AI_MATKEY_SHININESS, value) == AI_SUCCESS) {
            material.Ns = value;
        }
        if (sourceMaterial.Get(AI_MATKEY_REFRACTI, value) == AI_SUCCESS) {
            material.Ni = value;
        }
        if (sourceMaterial.Get(AI_MATKEY_OPACITY, value) == AI_SUCCESS) {
            material.d = value;
        }

        // PBR Material Properties
        aiColor4D baseColor;
        if (sourceMaterial.Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS) {
            material.Kd = glm::vec3(baseColor.r, baseColor.g, baseColor.b);
            material.d = baseColor.a;
        }
        if (sourceMaterial.Get(AI_MATKEY_ROUGHNESS_FACTOR, value) == AI_SUCCESS) {
            material.Pr = value;
        }
        if (sourceMaterial.Get(AI_MATKEY_METALLIC_FACTOR, value) == AI_SUCCESS) {
            material.Pm = value;
        }

        // Legacy Texture Maps
        material.map_Kd_index   = LoadMaterialTexture(sourceMaterial, aiTextureType_BASE_COLOR, context);
        material.map_Ka_index   = LoadMaterialTexture(sourceMaterial, aiTextureType_AMBIENT, context);
        material.map_Ks_index   = LoadMaterialTexture(sourceMaterial, aiTextureType_SPECULAR, context);
        material.map_Ns_index   = LoadMaterialTexture(sourceMaterial, aiTextureType_SHININESS, context);
        material.map_d_index    = LoadMaterialTexture(sourceMaterial, aiTextureType_OPACITY, context);

        // PBR Texture Maps
        material.map_Pr_index   = LoadMaterialTexture(sourceMaterial, aiTextureType_DIFFUSE_ROUGHNESS, context);
        material.map_Pm_index   = LoadMaterialTexture(sourceMaterial, aiTextureType_METALNESS, context);
        material.map_Ke_index   = LoadMaterialTexture(sourceMaterial, aiTextureType_EMISSION_COLOR, context);
        material.map_norm_index = LoadMaterialTexture(sourceMaterial, aiTextureType_NORMALS, context);

        // Fall back to legacy texture names
        if (material.map_Kd_index == -1) {
            material.map_Kd_index = LoadMaterialTexture(sourceMaterial, aiTextureType_DIFFUSE, context);
        }
        if (material.map_Ke_index == -1) {
            material.map_Ke_index = LoadMaterialTexture(sourceMaterial, aiTextureType_EMISSIVE, context);
        }

        context.mesh.materials.push_back(std::move(material));
    }

};



