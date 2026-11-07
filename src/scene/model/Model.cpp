#include <filesystem>
#include <iostream>
#include <memory>
#include <system_error>
#include <utility>

#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stb_image.h>

#include "Model.hpp"
#include "FacePrimitives.hpp"

bool Model::loadModel(const std::string& filePath)
{
    std::cout << "Loading Model at " + filePath << std::endl;
    std::string modelPath = filePath;

    Assimp::Importer importer;

    // Load static triangulated mesh ignoring animations
    importer.SetPropertyInteger(
        AI_CONFIG_PP_SBP_REMOVE,
        aiPrimitiveType_POINT |
        aiPrimitiveType_LINE);
    const aiScene* sourceScene = importer.ReadFile(filePath,
        aiProcess_Triangulate |
        aiProcess_SortByPType |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals |
        aiProcess_FlipUVs |
        aiProcess_PreTransformVertices |
        aiProcess_ValidateDataStructure
    );

    if (!sourceScene || (sourceScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !sourceScene->mRootNode) {
        std::cerr << "Failed to load model " + filePath + ": " + importer.GetErrorString() << std::endl;
        return false;
    }

    std::error_code pathError;
    const std::filesystem::path modelFileSystemPath = std::filesystem::absolute(filePath, pathError);
    if (pathError) {
        std::cerr << "Failed to resolve model path " << filePath << ": " << pathError.message() << std::endl;
        return false;
    }

    modelPath = modelFileSystemPath.lexically_normal().generic_string();
    textures.reserve(textures.size() + sourceScene->mNumTextures);
    materials.reserve(materials.size() + sourceScene->mNumMaterials);
    subMeshes.reserve(subMeshes.size() + sourceScene->mNumMeshes);

    const unsigned int materialOffset = static_cast<unsigned int>(materials.size());

    // Load materials and textures into the model
    for (unsigned int i = 0; i < sourceScene->mNumMaterials; i++) {
        if (!loadMaterial(modelPath, *sourceScene->mMaterials[i], *sourceScene)) 
            return false;
    }

    // Append imported triangle submeshes to the model
    for (unsigned int i = 0; i < sourceScene->mNumMeshes; i++) {
        if (sourceScene->mMeshes[i]->mPrimitiveTypes != aiPrimitiveType_TRIANGLE) 
            continue;
        if (!loadSubMesh(sourceScene->mMeshes[i], materialOffset))  
            return false;
    }

    // Create lightmap atlas for newly loaded mesh
    if (!lightmap.buildFromModel(*this)) {
        std::cerr << "Failed to unwrap light map at: " << filePath << std::endl;
        return false;
    }

    FacePrimitives primitives(*this);
    if (!bvh.BuildBvh<nFacesBvhLeaf>(primitives)) {
        return false;
    }

    return true;
}

bool Model::loadSubMesh(const aiMesh* sourceSubMesh, unsigned int materialOffset)
{
    if (!sourceSubMesh->HasPositions() || !sourceSubMesh->HasNormals()) {
        std::cerr << "Triangle mesh is missing positions or normals" << std::endl;
        return false;
    }

    // Get Start of Range for Submesh
    const unsigned int vertexStart   = static_cast<unsigned int>(vertices.size());
    const unsigned int indexStart    = static_cast<unsigned int>(faces.size()) * 3;
    const int          materialIndex = static_cast<int>(materialOffset + sourceSubMesh->mMaterialIndex);

    // Load Vertex Data
    vertices.reserve(vertices.size() + sourceSubMesh->mNumVertices);
    faces.reserve(faces.size() + sourceSubMesh->mNumFaces);
    faceData.reserve(faceData.size() + sourceSubMesh->mNumFaces);
    const bool hasUVs = sourceSubMesh->HasTextureCoords(0);
    for (unsigned int i = 0; i < sourceSubMesh->mNumVertices; i++) {
        Vertex vertex{};
        vertex.position = glm::vec3(
            sourceSubMesh->mVertices[i].x,
            sourceSubMesh->mVertices[i].y,
            sourceSubMesh->mVertices[i].z);
        vertex.normal = glm::vec3(
            sourceSubMesh->mNormals[i].x,
            sourceSubMesh->mNormals[i].y,
            sourceSubMesh->mNormals[i].z);
        vertex.materialTexCoords = glm::vec2(0.0f);
        vertex.lightMapTexCoords = glm::vec2(0.0f);
        if (hasUVs) {
            vertex.materialTexCoords = glm::vec2(
                sourceSubMesh->mTextureCoords[0][i].x,
                sourceSubMesh->mTextureCoords[0][i].y);
        }
        vertices.push_back(vertex);
    }

    // Load Face Indices
    for (unsigned int i = 0; i < sourceSubMesh->mNumFaces; i++) {
        const aiFace& face = sourceSubMesh->mFaces[i];
        if (face.mNumIndices != 3) {
            std::cerr << "Expected a triangle face" << std::endl;
            return false;
        }
        faces.push_back({
            vertexStart + face.mIndices[0],
            vertexStart + face.mIndices[1],
            vertexStart + face.mIndices[2]
        });
        faceData.push_back({static_cast<unsigned int>(materialIndex)});
    }

    // Range of indices in submesh
    subMeshes.push_back(SubMesh{indexStart, sourceSubMesh->mNumFaces * 3, materialIndex});
    return true;
}

bool Model::loadEmbeddedTexture(const std::string& modelPath, const aiScene& sourceScene, const aiTexture* sourceTexture, int& outTextureIndex)
{
    if (!sourceTexture || !sourceTexture->pcData) {
        outTextureIndex = -1;
        std::cerr << "Invalid embedded texture" << std::endl;
        return false;
    }

    // Get Index of embedded texture
    int sourceTexIndex = -1;
    std::string filePath;
    for (unsigned int i = 0; i < sourceScene.mNumTextures; i++) {
        if (sourceScene.mTextures[i] == sourceTexture) {
            sourceTexIndex = i;
            filePath = modelPath + "#embedded:*" + std::to_string(i);
        }
    }
    if (sourceTexIndex < 0) {
        outTextureIndex = -1;
        std::cerr << "Tried to load non-existent embedded texture" << std::endl;
        return false;
    }

    // Check if its already loaded
    bool contained = textureLibrary.find(filePath) != textureLibrary.end();
    if (contained) {
        outTextureIndex = textureLibrary[filePath];
        return true;
    }

    // Load From Raw Data
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
            outTextureIndex = -1;
            std::cerr << "Failed to decode embedded texture" << std::endl;
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

    textures.push_back(std::move(texture));
    textureLibrary[filePath] = textures.size()-1;
    outTextureIndex = textures.size()-1;
    return true;
}

bool Model::loadExternalTexture(const std::string& modelPath, std::string filePath, int& outTextureIndex)
{
    if (filePath.c_str()[0] == '*') {
        outTextureIndex = -1;
        std::cerr << "Embedded texture loaded as an external texture from: " + std::string(filePath.c_str()) << std::endl;
        return false;
    }

    // Handle relative file paths
    std::filesystem::path resolvedFileSystem;
    resolvedFileSystem = std::filesystem::path(filePath.c_str());
    if (resolvedFileSystem.is_relative()) {
        resolvedFileSystem = std::filesystem::path(modelPath).parent_path() / resolvedFileSystem;
    }
    std::error_code pathError;
    resolvedFileSystem = std::filesystem::weakly_canonical(resolvedFileSystem, pathError);
    if (pathError) {
        outTextureIndex = -1;
        std::cerr << "Failed to resolve texture path " << filePath.c_str() << ": " << pathError.message() << std::endl;
        return false;
    }
    std::string resolvedFilePath = resolvedFileSystem.generic_string();
    const std::string textureKey = modelPath + "#external:" + resolvedFilePath;

    // Check if already was added
    bool contained = textureLibrary.find(textureKey) != textureLibrary.end();
    if (contained) {
        outTextureIndex = textureLibrary[textureKey];
        return true;
    }

    // Load texture data from file path
    int width = 0, height = 0, channels = 4;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> texData(stbi_load(
        resolvedFilePath.c_str(),
        &width,
        &height,
        nullptr,
        channels
    ), stbi_image_free);

    if (!texData) {
        std::cerr << "Failed to load texture: " + resolvedFilePath << std::endl;
        outTextureIndex = -1;
        return false;
    }

    Texture2D texture;
    size_t dataCount = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
    texture.data.assign(texData.get(), texData.get() + dataCount);
    texture.metaData.width    = width;
    texture.metaData.height   = height;
    texture.metaData.channels = channels;
    texture.metaData.format   = TextureMetaData::RGBA8;

    textures.push_back(std::move(texture));
    textureLibrary[textureKey] = textures.size()-1;
    outTextureIndex = textures.size()-1;
    return true;
}

bool Model::loadMaterial(const std::string& modelPath, const aiMaterial& sourceMaterial, const aiScene& sourceScene)
{
    Material material;
    
    // Get Vector Data
    aiColor4D baseColor;
    if (sourceMaterial.Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS) {
        material.baseColor = glm::vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
    }
    aiColor3D emissive;
    if (sourceMaterial.Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
        material.emissive = glm::vec3(emissive.r, emissive.g, emissive.b);
    }

    // Get Scalar data
    sourceMaterial.Get(AI_MATKEY_METALLIC_FACTOR, material.metallic);
    sourceMaterial.Get(AI_MATKEY_ROUGHNESS_FACTOR, material.roughness);
    sourceMaterial.Get(AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_NORMALS, 0), material.normal);
    sourceMaterial.Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_LIGHTMAP, 0), material.occlusion);

    // Get Textures
    auto LoadMaterialTexture = [&](aiTextureType type, int& outTextureIndex) {
        aiString filePath;
        if (sourceMaterial.GetTexture(type, 0, &filePath) != AI_SUCCESS || filePath.length == 0) {
            outTextureIndex = -1; // There is no material texture (not an error)
            return true;
        }

        if (const aiTexture* embedded = sourceScene.GetEmbeddedTexture(filePath.C_Str())) {
            return loadEmbeddedTexture(modelPath, sourceScene, embedded, outTextureIndex);
        }
        return loadExternalTexture(modelPath, filePath.C_Str(), outTextureIndex);
    };

    if (!LoadMaterialTexture(aiTextureType_BASE_COLOR, material.baseColorTextureIndex))  {
        return false;
    }
    if (!LoadMaterialTexture(aiTextureType_GLTF_METALLIC_ROUGHNESS, material.metallicRoughnessTextureIndex))  {
        return false;
    }
    if (!LoadMaterialTexture(aiTextureType_LIGHTMAP, material.occlusionTextureIndex))  {
        return false;
    }
    if (!LoadMaterialTexture(aiTextureType_NORMALS, material.normalTextureIndex))      {
        return false;
    }
    if (!LoadMaterialTexture(aiTextureType_EMISSIVE, material.emissiveTextureIndex))   {
        return false;
    }

    // Other names occlusion texture could be under
    if (material.occlusionTextureIndex == -1) {
        if(!LoadMaterialTexture(aiTextureType_AMBIENT_OCCLUSION, material.occlusionTextureIndex))
            return false;
    }
    
    materials.push_back(std::move(material));
    return true;
}
