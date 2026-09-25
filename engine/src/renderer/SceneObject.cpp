#include <engine/renderer/SceneObject.hpp>

#include <glm/gtc/matrix_transform.hpp>

SceneObject::SceneObject(std::shared_ptr<Mesh> mesh)
    : mesh(mesh) {
}

glm::mat4 SceneObject::getLocalMatrix() const {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
    model = model * glm::mat4_cast(orientation);   // quaternion -> matrice de rotation
    model = glm::scale(model, scale);
    return model;
}

glm::vec3 SceneObject::getEulerDegrees() const {
    return glm::degrees(glm::eulerAngles(orientation));
}

void SceneObject::setEulerDegrees(const glm::vec3& deg) {
    orientation = glm::quat(glm::radians(deg));
}