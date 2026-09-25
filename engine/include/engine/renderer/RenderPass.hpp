#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class RenderPass {
public:
    RenderPass(VkDevice device, VkFormat imageFormat, VkFormat depthFormat);
    ~RenderPass();

    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

    VkRenderPass getHandle() const { return renderPass; }

private:
    void createRenderPass(VkFormat imageFormat, VkFormat depthFormat);

    VkDevice device;
    VkRenderPass renderPass;
};