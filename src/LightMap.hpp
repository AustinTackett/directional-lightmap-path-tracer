#pragma once

#include "Mesh.hpp"

/*
    Light map with generated uv's for entire model for baking global illumination
*/
struct LightMap
{
    unsigned int width  = 0;
    unsigned int height = 0;
};

bool BuildLightMapUvs(LightMap& lightMap, Mesh& mesh)
{
    
}