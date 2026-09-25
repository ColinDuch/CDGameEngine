#include <engine/renderer/UniformBuffer.hpp>
#include <engine/renderer/BufferUtils.hpp>
#include <cstring>

UniformBuffer::UniformBuffer(const VulkanContext& ctx) : device(ctx.device) {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    BufferUtils::createBuffer(
        ctx.physicalDevice, ctx.device, bufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        buffer, memory
    );

    vkMapMemory(device, memory, 0, bufferSize, 0, &mappedData);
}

UniformBuffer::~UniformBuffer() {
    vkUnmapMemory(device, memory);
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
}

void UniformBuffer::update(const UniformBufferObject& ubo) {
    memcpy(mappedData, &ubo, sizeof(ubo));
}
