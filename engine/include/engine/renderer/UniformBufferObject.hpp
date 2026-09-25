#pragma once

#include <glm/glm.hpp>

struct UniformBufferObject {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;

    alignas(16) glm::vec3 lightDirection;
    alignas(16) glm::vec3 lightColor;
    alignas(4)  float lightIntensity;
    alignas(4)  float ambientStrength;
    alignas(16) glm::vec3 cameraPosition;
    alignas(4)  float shininess;
    float exposure = 1.0f;
    alignas(16) glm::mat4 lightSpace{1.0f};
    float shadowStrength = 0.6f;
};