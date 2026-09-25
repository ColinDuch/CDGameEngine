#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

class InputMap;

class Camera {
public:
    Camera(glm::vec3 position);

    void processKeyboard(const InputMap& input, float deltaTime);
    void processMouseMovement(float xoffset, float yoffset);

    glm::mat4 getViewMatrix() const;
    glm::vec3 getPosition() const { return position; }
    float getYaw() const { return yaw; }
    float getPitch() const { return pitch; }
    void setState(glm::vec3 newPosition, float newYaw, float newPitch);

    float getFov() const { return fov; }
    void processMouseScroll(float yoffset);

    float getMovementSpeed() const { return movementSpeed; }
    void  setMovementSpeed(float s) { movementSpeed = s; }

private:
    float fov = 45.0f;

private:
    void updateVectors();

    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;

    float yaw;
    float pitch;

    float movementSpeed = 2.5f;
    float mouseSensitivity = 0.1f;
};