#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <engine/renderer/VulkanContext.hpp>
#include <string>

class Texture {
public:
    Texture(const VulkanContext& ctx, const std::string& filepath);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(const VulkanContext& ctx, const unsigned char* pixels, int width, int height);

    VkImageView getImageView() const { return imageView; }
    VkSampler getSampler() const { return sampler; }

private:
    void createTextureImage(const std::string& filepath, VkCommandPool commandPool, VkQueue graphicsQueue);
    void createTextureImageView();
    void createTextureSampler();
    void createTextureImageFromPixels(const unsigned char* pixels, int width, int height,
        VkCommandPool commandPool, VkQueue graphicsQueue);
    void generateMipmaps(VkCommandPool commandPool, VkQueue graphicsQueue,
        int texWidth, int texHeight);

    VkBuffer createStagingBuffer(const void* pixels, VkDeviceSize size, VkDeviceMemory& stagingMemory);
    void transitionImageLayout(VkCommandPool commandPool, VkQueue graphicsQueue, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkCommandPool commandPool, VkQueue graphicsQueue, VkBuffer buffer, uint32_t width, uint32_t height);

    VkPhysicalDevice physicalDevice;
    VkDevice device;

    VkImage image;
    VkDeviceMemory imageMemory;
    VkImageView imageView;
    VkSampler sampler;
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;   // devient float pour un .hdr
    uint32_t mipLevels = 1;
};