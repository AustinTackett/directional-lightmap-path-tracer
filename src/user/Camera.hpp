#pragma once

#include <algorithm>
#include <GLFW/glfw3.h>

#include "Transform.hpp"

struct Camera 
{
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 target  {0.0f, 0.0f, -1.0f};
    
    float fov;
    float near_plane = 0.01f;
    float far_plane;

    Camera(glm::vec3 initialPosition, float fovDegrees, float sceneRadius)
        : position(initialPosition), 
          target(initialPosition + (glm::length(initialPosition) > 0.0f
              ? glm::normalize(-initialPosition)
              : glm::vec3(0.0f, 0.0f, -1.0f))),
          fov(glm::radians(fovDegrees)),
          far_plane(sceneRadius * 20.0f)
    {}

    glm::mat4 getViewTransform()
    {
        return glm::lookAt(position, target, Transform::WorldY);
    }

    glm::mat4 getPerspectiveTransform(unsigned int windowWidth, unsigned int windowHeight)
    {
        return glm::perspective(fov, windowWidth/static_cast<float>(windowHeight), near_plane, far_plane);
    }

    void update(GLFWwindow* window, float speed) {
        const double currentTime = glfwGetTime();
        if (!cameraInputInitialized) {
            cameraTransform.position = position;
            cameraTransform.orientation = glm::normalize(glm::quat_cast(glm::mat3(glm::inverse(getViewTransform()))));
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            glfwGetCursorPos(window, &previousCursorX, &previousCursorY);
            previousCameraTime = currentTime;
            cameraInputInitialized = true;
        }
        
        float deltaTime = static_cast<float>(std::min(currentTime - previousCameraTime, 0.1));
        previousCameraTime = currentTime;

        auto keyPressed = [&](int key) { return glfwGetKey(window, key) == GLFW_PRESS; };
        bool altPressed = keyPressed(GLFW_KEY_LEFT_ALT) || keyPressed(GLFW_KEY_RIGHT_ALT);
        if (altPressed && !altPressedLastFrame) {
            bool isCursorHidden = glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
            glfwSetInputMode(window, GLFW_CURSOR, isCursorHidden ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            glfwGetCursorPos(window, &previousCursorX, &previousCursorY);
        }
        altPressedLastFrame = altPressed;

        double cursorX, cursorY;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        double cursorXDelta = cursorX - previousCursorX;
        double cursorYDelta = cursorY - previousCursorY;
        previousCursorX = cursorX;
        previousCursorY = cursorY;
        if (!glfwGetWindowAttrib(window, GLFW_FOCUSED)) return;

        glm::vec3 forwardDir = -cameraTransform.localZ();
        glm::vec3 rightDir = cameraTransform.localX();
        glm::vec3 forwardDirGrounded = glm::vec3(forwardDir.x, 0, forwardDir.z);
        if (glm::length(forwardDirGrounded) > 0.00001f)
            forwardDirGrounded = glm::normalize(forwardDirGrounded);

        /* Move Using WASD/Arrow Keys */
        if (keyPressed(GLFW_KEY_W) || keyPressed(GLFW_KEY_UP)) {
            cameraTransform.position += forwardDirGrounded * (speed * deltaTime);
        }
        else if (keyPressed(GLFW_KEY_S) || keyPressed(GLFW_KEY_DOWN)) {
            cameraTransform.position -= forwardDirGrounded * (speed * deltaTime);
        }
        if (keyPressed(GLFW_KEY_D) || keyPressed(GLFW_KEY_RIGHT)) {
            cameraTransform.position += rightDir * (speed * deltaTime);
        }
        else if (keyPressed(GLFW_KEY_A) || keyPressed(GLFW_KEY_LEFT)) {
            cameraTransform.position -= rightDir * (speed * deltaTime);
        }
        if (keyPressed(GLFW_KEY_SPACE)) {
            cameraTransform.position += Transform::WorldY * (speed * deltaTime);
        }
        if (keyPressed(GLFW_KEY_LEFT_SHIFT) || keyPressed(GLFW_KEY_RIGHT_SHIFT)) {
            cameraTransform.position -= Transform::WorldY * (speed * deltaTime);
        }

        /* Look at Mouse (disable when we need the cursor to adjust the window) */
        if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
            int windowWidth, windowHeight;
            glfwGetWindowSize(window, &windowWidth, &windowHeight);
            windowWidth = std::max(windowWidth, 1);
            windowHeight = std::max(windowHeight, 1);
            float mousePosX = static_cast<float>(cursorXDelta / windowWidth);
            float mousePosY = static_cast<float>(cursorYDelta / windowHeight);
            glm::quat phiRotation = glm::angleAxis(-mousePosX, Transform::WorldY);
            glm::quat thetaRotation = glm::angleAxis(-mousePosY, cameraTransform.localX());
            cameraTransform.orientation = glm::normalize(phiRotation * thetaRotation * cameraTransform.orientation);
        }

        position = cameraTransform.position;
        target = cameraTransform.position - cameraTransform.localZ();
    }

private:
    Transform cameraTransform;
    bool cameraInputInitialized = false;
    bool altPressedLastFrame = false;
    double previousCameraTime = 0.0;
    double previousCursorX = 0.0;
    double previousCursorY = 0.0;
};
