#include <engine/renderer/CommandPool.hpp>
#include <stdexcept>

CommandPool::CommandPool(VkDevice device, uint32_t graphicsFamilyIndex)
    : device(device) {

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsFamilyIndex;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
}

CommandPool::~CommandPool() {
    vkDestroyCommandPool(device, commandPool, nullptr);
}