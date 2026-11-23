#pragma once

#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>

struct alignas(16) DirectionalLight {
    alignas(16) glm::vec3 direction;
    alignas(16) glm::vec3 Irradiance; // Per RGB channel
};

static_assert(offsetof(DirectionalLight, direction) == 0);
static_assert(offsetof(DirectionalLight, Irradiance) == 16);
static_assert(sizeof(DirectionalLight) == 32);
