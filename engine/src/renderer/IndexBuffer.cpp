#include <engine/renderer/IndexBuffer.hpp>
#include <engine/renderer/BufferUtils.hpp>

IndexBuffer::IndexBuffer(const VulkanContext& ctx, const std::vector<uint32_t>& indices)
    : device(ctx.device), indexCount(static_cast<uint32_t>(indices.size())) {

    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    BufferUtils::createDeviceLocalBuffer(
        ctx.physicalDevice, ctx.device, ctx.commandPool, ctx.graphicsQueue,
        indices.data(), bufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        buffer, memory
    );
}

IndexBuffer::~IndexBuffer() {
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
}