#pragma once

#include <string>
#include <cstddef>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "Material.hpp"
#include "Texture2D.hpp"
#include "LightMap.hpp"
#include "Bvh.hpp"

/*
    Triangle Mesh Container Classes
*/

struct alignas(16) Vertex 
{
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 normal;
    alignas(8)  glm::vec2 materialTexCoords;
    alignas(8)  glm::vec2 lightMapTexCoords;
};

struct alignas(4) Face
{
    // Packed triangle indices; metadata is stored separately in FaceData.
    alignas(4) unsigned int vertexIndex0;
    alignas(4) unsigned int vertexIndex1;
    alignas(4) unsigned int vertexIndex2;
};

struct alignas(4) FaceData 
{
    alignas(4) unsigned int materialIndex;
};

struct SubMesh
{
    // One raster draw call per submesh
    unsigned int indexStart;
    unsigned int indexCount;
    int materialIndex = -1;
};

// check std 430 alignment
static_assert(sizeof(Vertex) == 48 && alignof(Vertex) == 16);
static_assert(offsetof(Vertex, lightMapTexCoords) == 40);
static_assert(sizeof(Face) == 12 && alignof(Face) == 4);
static_assert(sizeof(FaceData) == 4 && alignof(FaceData) == 4);
static_assert(offsetof(FaceData, materialIndex) == 0);

using TextureLibrary = std::unordered_map<std::string, int>;

struct aiMesh;
struct aiScene;
struct aiTexture;
struct aiMaterial;

struct Model
{
    static constexpr unsigned int nFacesBvhLeaf = 4;

    LightMap  lightmap;
    Bvh       bvh;
    
    std::vector<Vertex>    vertices;
    std::vector<Face>      faces;
    std::vector<FaceData>  faceData;
    std::vector<SubMesh>   subMeshes;

    std::vector<Material>  materials;
    std::vector<Texture2D> textures;
    TextureLibrary         textureLibrary;

    // Append imported geometry/materials to this model; preserve its transform.
    bool loadModel(const std::string& filePath);

private:
    bool loadSubMesh(const aiMesh* sourceSubMesh, unsigned int materialOffset);

    bool loadEmbeddedTexture(const std::string& modelPath, const aiScene& sourceScene, const aiTexture* sourceTexture, int& outTextureIndex);

    bool loadExternalTexture(const std::string& modelPath, std::string filePath, int& outTextureIndex);

    bool loadMaterial(const std::string& modelPath, const aiMaterial& sourceMaterial, const aiScene& sourceScene);
};
