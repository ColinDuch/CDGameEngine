#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>

class SyncObjects {
public:
    SyncObjects(VkDevice device, uint32_t imageCount);
    ~SyncObjects();

    SyncObjects(const SyncObjects&) = delete;
    SyncObjects& operator=(const SyncObjects&) = delete;

    VkSemaphore getImageAvailableSemaphore() const { return imageAvailableSemaphore; }
    VkSemaphore getRenderFinishedSemaphore(uint32_t imageIndex) const { return renderFinishedSemaphores[imageIndex]; }
    VkFence getInFlightFence() const { return inFlightFence; }

private:
    VkDevice device;
    VkSemaphore imageAvailableSemaphore;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    VkFence inFlightFence;
};