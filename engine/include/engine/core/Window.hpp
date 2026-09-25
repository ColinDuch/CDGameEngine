#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>
#include <vector>


class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const;
    void pollEvents();

    GLFWwindow* getHandle() const { return window; }

    static std::vector<const char*> getRequiredVulkanExtensions();

    bool wasResized() const { return framebufferResized; }
    void resetResizedFlag() { framebufferResized = false; }

    double getMouseDeltaX();
    double getMouseDeltaY();

    bool shouldExit() const { return exitRequested; }

    bool isCursorLocked() const { return cursorLocked; }
    void toggleCursorLock();

    double getScrollDelta();

    void cancelClose() { glfwSetWindowShouldClose(window, GLFW_FALSE); exitRequested = false; }

private:
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    double scrollDelta = 0.0;

private:
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* window;
    int width;
    int height;

    bool framebufferResized = false;

    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    bool firstMouse = true;

    bool exitRequested = false;

    bool cursorLocked = false;
    bool tabWasPressed = false;

};