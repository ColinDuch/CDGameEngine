#include <engine/renderer/Camera.hpp>

#include <engine/core/InputMap.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

Camera::Camera(glm::vec3 position)
    : position(position), worldUp(0.0f, 0.0f, 1.0f), yaw(-90.0f), pitch(0.0f) {
    updateVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, position + front, up);
}

void Camera::processKeyboard(const InputMap& input, float deltaTime) {
    float velocity = movementSpeed * deltaTime;
    if (input.isDown(ACTION_SPRINT)) velocity *= 4.0f;

    // value() est analogique : le stick donne une vitesse progressive, la touche donne 0 ou 1.
    position += front   * velocity * (input.value(ACTION_MOVE_FORWARD) - input.value(ACTION_MOVE_BACKWARD));
    position += right   * velocity * (input.value(ACTION_MOVE_RIGHT)   - input.value(ACTION_MOVE_LEFT));
    position += worldUp * velocity * (input.value(ACTION_MOVE_UP)      - input.value(ACTION_MOVE_DOWN));
}

void Camera::processMouseMovement(float xoffset, float yoffset) {
    xoffset *= mouseSensitivity;
    yoffset *= mouseSensitivity;

    yaw -= xoffset;
    pitch += yoffset;

    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    updateVectors();
}

void Camera::updateVectors() {
    glm::vec3 newFront;
    newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    newFront.y = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    newFront.z = sin(glm::radians(pitch));
    front = glm::normalize(newFront);

    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}

void Camera::processMouseScroll(float yoffset) {
    fov -= yoffset * 2.0f;

    if (fov < 10.0f) fov = 10.0f;
    if (fov > 90.0f) fov = 90.0f;
}

void Camera::setState(glm::vec3 newPosition, float newYaw, float newPitch) {
    position = newPosition;
    yaw = newYaw;
    pitch = newPitch;
    updateVectors();
}