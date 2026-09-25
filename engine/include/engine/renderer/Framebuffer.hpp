#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>

class Framebuffer {
public:
    Framebuffer(VkDevice device, VkRenderPass renderPass, const std::vector<VkImageView>& imageViews, VkImageView depthImageView, VkExtent2D extent);
    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    void recreate(VkRenderPass renderPass, const std::vector<VkImageView>& imageViews, VkImageView depthImageView, VkExtent2D extent);

    const std::vector<VkFramebuffer>& getHandles() const { return framebuffers; }

private:
    void createFramebuffers(VkRenderPass renderPass, const std::vector<VkImageView>& imageViews, VkImageView depthImageView, VkExtent2D extent);
    void cleanup();

    VkDevice device;
    std::vector<VkFramebuffer> framebuffers;
};