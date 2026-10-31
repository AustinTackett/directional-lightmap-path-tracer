#pragma once

#include "Transform.hpp"
#include "Bvh.hpp"
#include "Mesh.hpp"

/*
    Wraps all data relevant to a single static mesh
*/

struct Model 
{
    Transform transform;
    Bvh       bvh;
    Mesh      mesh;
    Texture2D lightMap;
};  