#pragma once

#include "Transform.hpp"

struct Camera 
{
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 target  {0.0f, 0.0f, -1.0f};
    
    float fov;
    float near_plane;
    float far_plane;

    glm::mat4 view()
    {
        return glm::lookAt(position, target, Transform::WorldY);
    }

    glm::mat4 perspective(unsigned int windowWidth, unsigned int windowHeight)
    {
        return glm::perspective(fov, windowWidth/static_cast<float>(windowHeight), near_plane, far_plane);
    }
};