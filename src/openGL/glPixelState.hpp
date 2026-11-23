#pragma once

#include <cstddef>
#include <GL/glew.h>
#include <glm/glm.hpp>

// std430 layout.
struct alignas(16) glPixelState
{
    glm::vec3 irradianceMean{0.0f};
    float     luminanceMean = 0.0f;
    float     lumSquaredDeviation = 0.0f; 
    GLuint    count = 0;
};

static_assert(sizeof(glPixelState) == 32);
static_assert(offsetof(glPixelState, luminanceMean) == 12);
static_assert(offsetof(glPixelState, lumSquaredDeviation) == 16);
static_assert(offsetof(glPixelState, count) == 20);
