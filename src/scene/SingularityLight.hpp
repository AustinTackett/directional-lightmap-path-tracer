#pragma once

#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>

struct alignas(16) SingularityLight {
    enum LightType : std::uint32_t {
        DIRECTIONAL_LIGHT,
        POINT_LIGHT
    };

    std::uint32_t lightType;
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 direction;
    alignas(16) glm::vec3 irradiance; // Per RGB channel
};

static_assert(offsetof(SingularityLight, lightType) == 0);
static_assert(offsetof(SingularityLight, position) == 16);
static_assert(offsetof(SingularityLight, direction) == 32);
static_assert(offsetof(SingularityLight, irradiance) == 48);
static_assert(sizeof(SingularityLight) == 64);
