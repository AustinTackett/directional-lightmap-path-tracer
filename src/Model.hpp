#pragma once

#include <string>

#include "Transform.hpp"
#include "Bvh.hpp"
#include "Mesh.hpp"
#include "LightMap.hpp"

/*
    Wraps all data relevant to a single static mesh
*/

struct Model 
{
    Transform transform;
    Bvh       bvh;
    Mesh      mesh;
    LightMap  lightMap;
};  

bool LoadModel(Model& outModel, std::string meshPath) 
{
    outModel.transform = Transform{};
    
    if (!LoadMesh(meshPath, outModel.mesh)) {
        return false;
    }
    
    if (!BuildBvh(outModel.bvh, outModel.mesh)) {
        return false;
    }
    
    // build light path
    
    return true;
}
