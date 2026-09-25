#include <engine/renderer/Framebuffer.hpp>
#include <stdexcept>
#include <array>

Framebuffer::Framebuffer(VkDevice device, VkRenderPass renderPass, const std::vector<VkImageView>& imageViews, VkImageView depthImageView, VkExtent2D extent) : device(device) {
    createFramebuffers(renderPass, imageViews, depthImageView, extent);
}

Framebuffer::~Framebuffer() {
    cleanup();
}

void Framebuffer::recreate(VkRenderPass renderPass, const std::vector<VkImageView>& imageViews, VkImageView depthImageView, VkExtent2D extent) {
    cleanup();
    createFramebuffers(renderPass, imageViews, depthImageView, extent);
}

void Framebuffer::cleanup() {
    for (auto framebuffer : framebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    framebuffers.clear();
}

void Framebuffer::createFramebuffers(VkRenderPass renderPass, const std::vector<VkImageView>& imageViews, VkImageView depthImageView, VkExtent2D extent) {
    framebuffers.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = { imageViews[i], depthImageView };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}