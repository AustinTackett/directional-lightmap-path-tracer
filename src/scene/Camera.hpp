#pragma once

#include "Transform.hpp"

struct Camera 
{
    Transform transform;
    float fov;
    float near_plane;
    float far_plane;
};