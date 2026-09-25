#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <engine/renderer/VulkanContext.hpp>
#include <vector>
#include <cstdint>

class IndexBuffer {
public:
    IndexBuffer(const VulkanContext& ctx, const std::vector<uint32_t>& indices);
    ~IndexBuffer();

    IndexBuffer(const IndexBuffer&) = delete;
    IndexBuffer& operator=(const IndexBuffer&) = delete;

    VkBuffer getHandle() const { return buffer; }
    uint32_t getIndexCount() const { return indexCount; }

private:
    VkDevice device;
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint32_t indexCount;
};