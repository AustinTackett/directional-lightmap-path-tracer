#pragma once

#include <memory>
#include <vector>

#include "Textures.hpp"
#include "TriMesh.hpp"
#include "Material.hpp"

/*
    Model parent class that can hold multiple meshes, materials, and other parameters
    
    -> TriMeshes hold indices to Materials in the Model class
    -> Materials hold indices to Textures in the Model Class.
*/

struct Model
{
    // CPU representation uses pointers, but gets compressed into a linear array for the GPU
    std::vector<std::shared_ptr<TriMesh>>   triMeshes;
    std::vector<std::shared_ptr<Material>>  materials;
    std::vector<std::shared_ptr<Texture2D>> textures; 
};
