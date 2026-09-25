#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace BufferUtils {
    uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);

    void createBuffer(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);

        // Cree un buffer DEVICE_LOCAL rempli avec `data` via un staging buffer temporaire.
        // `usage` doit inclure l'usage final (VERTEX_BUFFER_BIT, INDEX_BUFFER_BIT, ...) ;
        // TRANSFER_DST_BIT est ajoute automatiquement.
        void createDeviceLocalBuffer(VkPhysicalDevice physicalDevice, VkDevice device,
            VkCommandPool commandPool, VkQueue graphicsQueue,
            const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
            VkBuffer& buffer, VkDeviceMemory& bufferMemory);
}