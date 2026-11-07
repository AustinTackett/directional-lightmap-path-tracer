#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/ext/matrix_transform.hpp>

// Special Component that the render also looks at
struct Transform
{
    static constexpr glm::vec3 WorldX{1.0f, 0.0f, 0.0f};
    static constexpr glm::vec3 WorldY{0.0f, 1.0f, 0.0f};
    static constexpr glm::vec3 WorldZ{0.0f, 0.0f, 1.0f};

    glm::quat orientation{1,0,0,0};
    glm::vec3 position{0,0,0};
    glm::vec3 scale{1,1,1};

    Transform(): 
        orientation(1.0f, 0.0f, 0.0f, 0.0f),
        position(0.0f, 0.0f, 0.0f),
        scale(1.0f, 1.0f, 1.0f)
        {}

    Transform(glm::vec3 position):
        orientation(1.0f, 0.0f, 0.0f, 0.0f),
        position(position),
        scale(1.0f, 1.0f, 1.0f)
        {} 

    Transform(float phi, float theta, float radius):
        orientation(1.0f, 0.0f, 0.0f, 0.0f),
        position(0.0f, 0.0f, 1.0f),
        scale(1.0f, 1.0f, 1.0f)
        {
            orbitOrigin(phi, theta, radius);
        }

    Transform(glm::quat orientation, glm::vec3 position, glm::vec3 scale):
        orientation(orientation),
        position(position),
        scale(scale)
        {} 

    glm::vec3 localX()
    {
        return glm::normalize(orientation * WorldX);
    }

    glm::vec3 localY()
    {
        return glm::normalize(orientation * WorldY);
    }

    glm::vec3 localZ()
    {
        return glm::normalize(orientation * WorldZ);
    }

    void translate(glm::vec3 deltaPosition) { position += deltaPosition; }

    void rotate(glm::quat rotationDelta) { orientation = rotationDelta * orientation; }

    void rotate(float angle, glm::vec3 axis)
    {
        glm::quat rotationDelta = glm::angleAxis(angle, axis);
        orientation = rotationDelta * orientation;
    }        

    void orbitOrigin(float phiDelta, float thetaDelta, float radiusDelta)
    {
        glm::vec3 thetaDirection = glm::normalize(orientation * WorldX);
        glm::vec3 phiDirection =  WorldY;

        glm::quat rotationTheta = glm::angleAxis(thetaDelta, thetaDirection);
        glm::quat rotationPhi = glm::angleAxis(phiDelta, phiDirection);
        
        orientation = rotationPhi * rotationTheta * orientation;
        orientation = glm::normalize(orientation);

        glm::vec3 radiusDirection = glm::normalize(orientation * WorldZ);
        float currentRadius = glm::length(position);
        currentRadius = glm::max(0.0f, radiusDelta + currentRadius);
        position = currentRadius*radiusDirection;
    }

    glm::mat4 getModelTransform() const
    {
        glm::mat4 translation = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 rotation = glm::mat4_cast(orientation);
        glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), scale);
        return translation * rotation * scaleMat;
    }

    glm::mat3 getNormalTransform() const 
    {
        return glm::transpose(glm::inverse(glm::mat3(getModelTransform())));
    }
};
