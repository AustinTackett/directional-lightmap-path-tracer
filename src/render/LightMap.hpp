#pragma once

struct Scene;

struct LightMap
{
    const unsigned int resolution = 0; // Single lightmap with dynamic size
    unsigned int width  = 0;
    unsigned int height = 0;
};

bool BuildFromScene(Scene& scene, LightMap& lightMap);
