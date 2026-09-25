#include <engine/core/Window.hpp>
#include <stdexcept>

Window::Window(int width, int height, const std::string& title)
    : width(width), height(height) {

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);

    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    glfwSetScrollCallback(window, scrollCallback);

    if (!window) {
        throw std::runtime_error("Échec de la création de la fenêtre GLFW !");
    }
}

void Window::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    self->framebufferResized = true;
}

Window::~Window() {
    glfwDestroyWindow(window);
    glfwTerminate();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window);
}

void Window::pollEvents() {
    glfwPollEvents();

    bool tabPressed = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
    if (tabPressed && !tabWasPressed) {
        toggleCursorLock();
    }
    tabWasPressed = tabPressed;
}

std::vector<const char*> Window::getRequiredVulkanExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    return std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionCount);
}

double Window::getMouseDeltaX() {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    if (firstMouse) {
        lastMouseX = xpos;
        lastMouseY = ypos;
        firstMouse = false;
    }

    double deltaX = xpos - lastMouseX;
    lastMouseX = xpos;
    return deltaX;
}

double Window::getMouseDeltaY() {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    double deltaY = lastMouseY - ypos;
    lastMouseY = ypos;
    return deltaY;
}

void Window::toggleCursorLock() {
    cursorLocked = !cursorLocked;
    glfwSetInputMode(window, GLFW_CURSOR, cursorLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void Window::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    self->scrollDelta += yoffset;
}

double Window::getScrollDelta() {
    double delta = scrollDelta;
    scrollDelta = 0.0;
    return delta;
}