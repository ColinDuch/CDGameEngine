#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class Surface {
public:
    Surface(VkInstance instance, GLFWwindow* window);
    ~Surface();

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    VkSurfaceKHR getHandle() const { return surface; }

private:
    VkInstance instance;
    VkSurfaceKHR surface;
};