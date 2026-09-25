#include <engine/renderer/VertexBuffer.hpp>
#include <engine/renderer/BufferUtils.hpp>

VertexBuffer::VertexBuffer(const VulkanContext& ctx, const std::vector<Vertex>& vertices)
    : device(ctx.device), vertexCount(static_cast<uint32_t>(vertices.size())) {

    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    BufferUtils::createDeviceLocalBuffer(
        ctx.physicalDevice, ctx.device, ctx.commandPool, ctx.graphicsQueue,
        vertices.data(), bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        buffer, memory
    );
}

VertexBuffer::~VertexBuffer() {
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
}