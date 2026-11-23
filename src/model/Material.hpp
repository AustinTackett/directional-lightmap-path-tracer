#pragma once
#include <cstddef>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

/*
    Core glTF 2.0 metallic-roughness material (no extensions).
    https://www.khronos.org/gltf/pbr/
*/

struct alignas(16) Material 
{
    alignas(16) glm::vec4 baseColor {1.0f, 1.0f, 1.0f, 1.0f};
    alignas(16) glm::vec3 emissive  {0.0f, 0.0f, 0.0f};
    alignas(4) float metallic  = 1.0f;
    alignas(4) float roughness = 1.0f;
    alignas(4) float normal    = 1.0f;
    alignas(4) float occlusion = 1.0f;

    alignas(4) int baseColorTextureIndex = -1;
    alignas(4) int normalTextureIndex    = -1;
    alignas(4) int emissiveTextureIndex  = -1;

    // GLTF texture packing (metallic/roughness may or may not be in same texture as occlusion)
    alignas(4) int metallicRoughnessTextureIndex = -1; // G channel = roughness, B channel = metallic
    alignas(4) int occlusionTextureIndex         = -1; // R channel
    alignas(4) int alphaMode = 0; // glTF: 0 = OPAQUE, 1 = MASK, 2 = BLEND
    alignas(4) float alphaCutoff = 0.5f;
};

// check std 430 alignment
static_assert(sizeof(Material) == 80 && alignof(Material) == 16);
static_assert(offsetof(Material, baseColor) == 0 && offsetof(Material, emissive) == 16);
static_assert(offsetof(Material, metallic) == 28 && offsetof(Material, roughness) == 32);
static_assert(offsetof(Material, baseColorTextureIndex) == 44);
static_assert(offsetof(Material, occlusionTextureIndex) == 60);
static_assert(offsetof(Material, alphaMode) == 64 && offsetof(Material, alphaCutoff) == 68);
