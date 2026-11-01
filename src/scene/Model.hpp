#pragma once

#include "Transform.hpp"
#include "Mesh.hpp"
#include "Bvh.hpp"

struct Model 
{
    Transform transform;
    Mesh      mesh;
    Bvh       bvh;
};