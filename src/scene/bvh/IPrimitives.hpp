#pragma once

#include <glm/vec3.hpp>

/*
    If a you implement this, then you can construct a bvh from that data
*/

struct Aabb;

struct IPrimitives 
{
    virtual unsigned int size() = 0;
    virtual glm::vec3 getCentroid(unsigned int primitiveIndex) = 0;
    virtual Aabb getAabb(unsigned int primitiveStart, unsigned int primitiveCount) = 0;
};
