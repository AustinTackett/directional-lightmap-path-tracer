#pragma once

#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "Material.hpp"
#include "Textures.hpp"
#include <glm/common.hpp>

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
    std::vector<VertexData>         vertices;
    std::vector<FaceVertexIndices>  faceVertexIndices;
    std::vector<FaceData>           faceData;
    std::vector<Texture2D>          textures;
    std::vector<Material>           materials;
    std::vector<SubMesh>            subMeshes;
};
