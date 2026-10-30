#pragma once

#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "Material.hpp"
#include "Textures.hpp"

/* 
    Triangle Mesh Container Classes
*/

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 texCoords;
};

// Three indices of vertices that are on face
struct Face 
{
    unsigned int index0;
    unsigned int index1;
    unsigned int index2;
    int materialIndex = -1;
};

// Set of indices that form a mesh of a single material
struct SubMesh
{
    unsigned int indexStart;
    unsigned int indexCount;
    int materialIndex = -1;
};

struct Mesh
{
    // Rasterization
    std::vector<unsigned int> indices;  
    std::vector<Vertex>       vertices;
    std::vector<SubMesh>      subMeshes;
    std::vector<Material>     materials;
    std::vector<Texture2D>    textures; 

    // Ray Tracing
    std::vector<Face> faces;
    Bvh bvh;
};