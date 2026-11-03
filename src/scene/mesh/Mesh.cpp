#include <stddef.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <system_error>
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

#include "Texture2D.hpp"
#include "Mesh.hpp"
#include "Scene.hpp"

/*
    Loading Mesh Data to the scene
*/

// Static Objects Only, no Animation Support
bool LoadMesh(Scene& destScene, const std::string& filePath)
{
    std::cout << "Loading Mesh at " + filePath + "\n";
    std::string meshPath = filePath;

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
    const std::filesystem::path meshFileSystemPath = std::filesystem::absolute(filePath, pathError);
    if (pathError) {
        std::cerr << "Failed to resolve mesh path " << filePath << ": " << pathError.message() << '\n';
        return false;
    }

    meshPath = meshFileSystemPath.lexically_normal().generic_string();
    destScene.textures.reserve(destScene.textures.size() + scene->mNumTextures);
    destScene.materials.reserve(destScene.materials.size() + scene->mNumMaterials);
    destScene.subMeshes.reserve(destScene.subMeshes.size() + scene->mNumMeshes);

    Mesh mesh;
    mesh.meshIndex      = static_cast<unsigned int>(destScene.meshes.size());
    mesh.vertexOffset   = static_cast<unsigned int>(destScene.vertices.size());
    mesh.faceOffset     = static_cast<unsigned int>(destScene.faces.size());
    mesh.materialOffset = static_cast<unsigned int>(destScene.materials.size());
    mesh.subMeshOffset  = static_cast<unsigned int>(destScene.subMeshes.size());
    
    // Add Materials and Textures to Mesh
    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        if (!ProcessMaterial(destScene, meshPath, *scene->mMaterials[i], *scene)) return false;
    }

    // Add Triangle Meshes to Mesh
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        if (scene->mMeshes[i]->mPrimitiveTypes != aiPrimitiveType_TRIANGLE) continue;
        if (!LoadSubMesh(destScene, mesh, scene->mMeshes[i])) return false;
    }

    mesh.vertexCount   = static_cast<unsigned int>(destScene.vertices.size())  - mesh.vertexOffset;
    mesh.faceCount     = static_cast<unsigned int>(destScene.faces.size())     - mesh.faceOffset;
    mesh.materialCount = static_cast<unsigned int>(destScene.materials.size()) - mesh.materialOffset;
    mesh.subMeshCount  = static_cast<unsigned int>(destScene.subMeshes.size()) - mesh.subMeshOffset;
    destScene.meshes.push_back(mesh);
    return true;
}

/*
    Geometry Processing 
*/

bool LoadSubMesh(Scene& destScene, const Mesh& mesh, const aiMesh* sourceMesh)
{
    if (!sourceMesh->HasPositions() || !sourceMesh->HasNormals()) {
        std::cerr << "Triangle mesh is missing positions or normals" << '\n';
        return false;
    }

    // Get Start of Range for Submesh
    const unsigned int vertexStart   = static_cast<unsigned int>(destScene.vertices.size()) - mesh.vertexOffset;
    const unsigned int indexStart    = (static_cast<unsigned int>(destScene.faces.size()) - mesh.faceOffset) * 3;
    const int          materialIndex = static_cast<int>(sourceMesh->mMaterialIndex);

    // Load Vertex Data
    destScene.vertices.reserve(destScene.vertices.size() + sourceMesh->mNumVertices);
    destScene.faces.reserve(destScene.faces.size() + sourceMesh->mNumFaces);
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
        vertex.materialTexCoords = glm::vec3(0.0f);
        vertex.lightMapTexCoords = glm::vec3(0.0f);
        if (hasUVs) {
            vertex.materialTexCoords = glm::vec3(
                sourceMesh->mTextureCoords[0][i].x,
                sourceMesh->mTextureCoords[0][i].y,
                sourceMesh->mTextureCoords[0][i].z);
        }
        destScene.vertices.push_back(vertex);
    }

    // Load Face Indices
    for (unsigned int i = 0; i < sourceMesh->mNumFaces; i++) {
        const aiFace& face = sourceMesh->mFaces[i];
        if (face.mNumIndices != 3) {
            std::cerr << "Expected a triangle face" << '\n';
            return false;
        }
        destScene.faces.push_back({
            vertexStart + face.mIndices[0],
            vertexStart + face.mIndices[1],
            vertexStart + face.mIndices[2],
            sourceMesh->mMaterialIndex
        });
    }

    // Range of indices in submesh
    destScene.subMeshes.push_back(SubMesh{indexStart, sourceMesh->mNumFaces * 3, materialIndex});
    return true;
}

/*
    Texture and material loading
*/

bool LoadEmbeddedTexture(Scene& destScene, const std::string& meshPath, const aiScene& sourceScene, const aiTexture* sourceTexture, int& outTextureIndex)
{
    if (!sourceTexture || !sourceTexture->pcData) {
        outTextureIndex = -1;
        std::cerr << "Invalid embedded texture" << '\n';
        return false;
    }

    // Get Index of embedded texture
    int sourceTexIndex = -1;
    std::string filePath;
    for (unsigned int i = 0; i < sourceScene.mNumTextures; i++) {
        if (sourceScene.mTextures[i] == sourceTexture) {
            sourceTexIndex = i;
            filePath = meshPath + "#embedded:*" + std::to_string(i);
        }
    }
    if (sourceTexIndex < 0) {
        outTextureIndex = -1;
        std::cerr << "Tried to load non-existent embedded texture" << '\n';
        return false;
    }

    // Check if its already loaded
    bool contained = destScene.textureLibrary.find(filePath) != destScene.textureLibrary.end();
    if (contained) {
        outTextureIndex = destScene.textureLibrary[filePath];
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

    destScene.textures.push_back(std::move(texture));
    destScene.textureLibrary[filePath] = destScene.textures.size()-1;
    outTextureIndex = destScene.textures.size()-1;
    return true;
}

bool LoadExternalTexture(Scene& destScene, const std::string& meshPath, std::string filePath, int& outTextureIndex)
{
    if (filePath.c_str()[0] == '*') {
        outTextureIndex = -1;
        std::cerr << "Embedded texture loaded as an external texture from: " + std::string(filePath.c_str()) << '\n';
        return false;
    }

    // Handle relative file paths
    std::filesystem::path resolvedFileSystem;
    resolvedFileSystem = std::filesystem::path(filePath.c_str());
    if (resolvedFileSystem.is_relative()) {
        resolvedFileSystem = std::filesystem::path(meshPath).parent_path() / resolvedFileSystem;
    }
    std::error_code pathError;
    resolvedFileSystem = std::filesystem::weakly_canonical(resolvedFileSystem, pathError);
    if (pathError) {
        outTextureIndex = -1;
        std::cerr << "Failed to resolve texture path " << filePath.c_str() << ": " << pathError.message() << '\n';
        return false;
    }
    std::string resolvedFilePath = resolvedFileSystem.generic_string();
    const std::string textureKey = meshPath + "#external:" + resolvedFilePath;

    // Check if already was added
    bool contained = destScene.textureLibrary.find(textureKey) != destScene.textureLibrary.end();
    if (contained) {
        outTextureIndex = destScene.textureLibrary[textureKey];
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
        std::cerr << "Failed to load texture: " + resolvedFilePath << '\n';
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

    destScene.textures.push_back(std::move(texture));
    destScene.textureLibrary[textureKey] = destScene.textures.size()-1;
    outTextureIndex = destScene.textures.size()-1;
    return true;
}

bool ProcessMaterial(Scene& destScene, const std::string& meshPath, const aiMaterial& sourceMaterial, const aiScene& scene)
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

        if (const aiTexture* embedded = scene.GetEmbeddedTexture(filePath.C_Str())) {
            return LoadEmbeddedTexture(destScene, meshPath, scene, embedded, outTextureIndex);
        }
        return LoadExternalTexture(destScene, meshPath, filePath.C_Str(), outTextureIndex);
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
    
    destScene.materials.push_back(std::move(material));
    return true;
}
