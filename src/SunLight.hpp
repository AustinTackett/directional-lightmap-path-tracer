#pragma once

#include <glm/vec3.hpp>

struct SunLight {
    glm::vec3 direction;
    glm::vec3 irradiance; // Per RGB channel
};