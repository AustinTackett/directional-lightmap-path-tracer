#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Transform.hpp"
#include "mesh/Mesh.hpp"
#include "mesh/Material.hpp"
#include "mesh/Texture2D.hpp"

/*
    Contains all the scene data that is not specific to the renderer
*/

// Map texture file path to specific index to avoid reloading
using TextureLibrary = std::unordered_map<std::string, int>;

struct Scene
{
    std::vector<Vertex>    vertices;
    std::vector<Face>      faces;
    std::vector<Mesh>      meshes;
    std::vector<SubMesh>   subMeshes;
    std::vector<Transform> meshTransforms;

    std::vector<Material>  materials;
    std::vector<Texture2D> textures;
    TextureLibrary         textureLibrary;
};