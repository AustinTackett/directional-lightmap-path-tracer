#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

// Core glTF 2.0 metallic-roughness material (no extensions).
// https://www.khronos.org/gltf/pbr/
struct Material
{
    glm::vec4 baseColor {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 emissive  {0.0f, 0.0f, 0.0f};
    float metallic  = 1.0f;
    float roughness = 1.0f;
    float normal    = 1.0f;
    float occlusion = 1.0f;

    int baseColorTextureIndex = -1;
    int normalTextureIndex    = -1;
    int emissiveTextureIndex  = -1;

    // GLTF texture packing
    int metallicRoughnessTextureIndex = -1; // G channel = roughness, B channel = metallic
    int occlusionTextureIndex = -1;         // R channel
};
