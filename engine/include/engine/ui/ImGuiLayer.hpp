#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class ImGuiLayer {
public:
    ImGuiLayer(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physicalDevice,
               VkDevice device, uint32_t graphicsQueueFamily, VkQueue graphicsQueue,
               VkRenderPass renderPass, uint32_t imageCount, VkCommandPool commandPool);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void beginFrame();
    void render(VkCommandBuffer commandBuffer);

private:
    VkDevice device;
    VkDescriptorPool descriptorPool;
};