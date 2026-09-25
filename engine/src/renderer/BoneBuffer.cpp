#include <engine/renderer/BoneBuffer.hpp>
#include <engine/renderer/BufferUtils.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>

VkDescriptorSetLayout BoneBuffer::createLayout(VkDevice device) {
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings = &b;

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("BoneBuffer : creation du layout impossible");
    }
    return layout;
}

BoneBuffer::BoneBuffer(const VulkanContext& ctx, VkDescriptorPool pool,
                       VkDescriptorSetLayout layout)
    : device(ctx.device) {

    VkDeviceSize size = sizeof(glm::mat4) * MaxBones;

    // HOST_VISIBLE mappe en permanence : reecrit a chaque frame en animation.
    BufferUtils::createBuffer(
        ctx.physicalDevice, ctx.device, size,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        buffer, memory);
    vkMapMemory(device, memory, 0, size, 0, &mapped);

    // Identite par defaut : un set jamais rempli ne deforme rien.
    std::vector<glm::mat4> identity(MaxBones, glm::mat4(1.0f));
    std::memcpy(mapped, identity.data(), size);

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = pool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &layout;
    if (vkAllocateDescriptorSets(device, &alloc, &set) != VK_SUCCESS) {
        throw std::runtime_error("BoneBuffer : allocation du descriptor set impossible");
    }

    VkDescriptorBufferInfo bi{};
    bi.buffer = buffer;
    bi.offset = 0;
    bi.range = size;

    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
}

BoneBuffer::~BoneBuffer() {
    if (mapped) { vkUnmapMemory(device, memory); mapped = nullptr; }
    if (buffer) vkDestroyBuffer(device, buffer, nullptr);
    if (memory) vkFreeMemory(device, memory, nullptr);
}

void BoneBuffer::upload(const std::vector<glm::mat4>& mats) {
    if (!mapped) return;
    size_t n = std::min<size_t>(mats.size(), MaxBones);
    if (n) std::memcpy(mapped, mats.data(), n * sizeof(glm::mat4));
}