#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <engine/renderer/VulkanContext.hpp>
#include <engine/renderer/Vertex.hpp>
#include <vector>

class VertexBuffer {
public:
    VertexBuffer(const VulkanContext& ctx, const std::vector<Vertex>& vertices);
    ~VertexBuffer();

    VertexBuffer(const VertexBuffer&) = delete;
    VertexBuffer& operator=(const VertexBuffer&) = delete;

    VkBuffer getHandle() const { return buffer; }
    uint32_t getVertexCount() const { return vertexCount; }

private:
    VkDevice device;
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint32_t vertexCount;
};