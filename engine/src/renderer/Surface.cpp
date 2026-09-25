#include <engine/renderer/Surface.hpp>
#include <stdexcept>

Surface::Surface(VkInstance instance, GLFWwindow* window) : instance(instance) {

    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
}

Surface::~Surface() {
    vkDestroySurfaceKHR(instance, surface, nullptr);
}