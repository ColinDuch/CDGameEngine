#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <engine/renderer/VulkanContext.hpp>

class DepthResources {
public:
    DepthResources(const VulkanContext& ctx, VkExtent2D extent);
    ~DepthResources();

    DepthResources(const DepthResources&) = delete;
    DepthResources& operator=(const DepthResources&) = delete;

    void recreate(VkExtent2D extent);

    VkImageView getImageView() const { return imageView; }
    VkFormat getFormat() const { return format; }

    static VkFormat findDepthFormat(VkPhysicalDevice physicalDevice);

private:
    void create(VkExtent2D extent);
    void cleanup();

    VkPhysicalDevice physicalDevice;
    VkDevice device;

    VkImage image;
    VkDeviceMemory memory;
    VkImageView imageView;
    VkFormat format;
};