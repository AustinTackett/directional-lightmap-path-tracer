#pragma once

#include <limits>

#include <glm/common.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct alignas(16) Aabb
{
    alignas(16) glm::vec3 min = glm::vec3 { std::numeric_limits<float>::max()    };
    alignas(16) glm::vec3 max = glm::vec3 { std::numeric_limits<float>::lowest() };

    float getSurfaceArea() const 
    {
        glm::vec3 extents = max - min;
        if (extents.x < 0 || extents.y < 0 || extents.z < 0) return 0;
        return 2.0f * (extents.x * extents.y + extents.y * extents.z + extents.z * extents.x);
    };

    unsigned int getLongestAxis() const 
    {
        glm::vec3 extents = max - min;
        if (extents.x >= extents.y && extents.x >= extents.z) return 0;
        if (extents.y >= extents.z) return 1;
        return 2;
    }

    void expand(const Aabb& aabb)
    {
        min = glm::min(min, aabb.min);
        max = glm::max(max, aabb.max);
    }

    glm::vec3 getCentroid() const
    {
        return min * 0.5f + max * 0.5f;
    }

    // Transform these bounds in place using an affine matrix.
    void applyMatrix(const glm::mat4& matrix)
    {
        Aabb transformed;
        for (unsigned int corner = 0; corner < 8; ++corner) {
            glm::vec3 point(
                (corner & 1) ? max.x : min.x,
                (corner & 2) ? max.y : min.y,
                (corner & 4) ? max.z : min.z);
            point = glm::vec3(matrix * glm::vec4(point, 1.0f));
            transformed.expand(Aabb{point, point});
        }
        *this = transformed;
    }
};
