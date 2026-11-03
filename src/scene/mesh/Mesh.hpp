#pragma once

#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

/*
    Triangle Mesh Container Classes
*/

struct alignas(16) Vertex
{
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 normal;
    glm::vec2   materialTexCoords;
    glm::vec2   lightMapTexCoords;
};

struct Face
{
    // One SSBO record per triangle, including its material.
    unsigned int vertexIndex0;
    unsigned int vertexIndex1;
    unsigned int vertexIndex2;
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
    unsigned int meshIndex = 0;

    // Location of data within scene
    unsigned int vertexOffset  = 0, vertexCount  = 0;
    unsigned int faceOffset    = 0, faceCount    = 0;
    unsigned int subMeshOffset = 0, subMeshCount = 0;

    unsigned int materialOffset = 0, materialCount = 0;
    
    unsigned int blasNodeOffset  = 0, blasNodeCount  = 0;
    unsigned int blasIndexOffset = 0, blasIndexCount = 0;
};


struct Scene;
struct aiMesh;
struct aiScene;
struct aiTexture;
struct aiMaterial;

bool LoadMesh(Scene& destination, const std::string& filePath);

bool LoadSubMesh(Scene& destination, const Mesh& mesh, const aiMesh* sourceMesh);

bool LoadEmbeddedTexture(Scene& destination, const std::string& meshPath, const aiScene& sourceScene, const aiTexture* sourceTexture, int& outTextureIndex);

bool LoadExternalTexture(Scene& destination, const std::string& meshPath, std::string filePath, int& outTextureIndex);

bool ProcessMaterial(Scene& destination, const std::string& meshPath, const aiMaterial& sourceMaterial, const aiScene& scene);
