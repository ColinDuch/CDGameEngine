#include <engine/renderer/EnvironmentMap.hpp>

#include <stdexcept>
#include <vector>
#include <array>

namespace {

// Ciel degrade par defaut (equirectangulaire), tant qu'aucun HDRI n'est charge.
std::vector<unsigned char> generateDefaultSky(int w, int h) {
    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        float t = static_cast<float>(y) / (h - 1);      // 0 = haut (ciel)
        float r = 0.45f * (1.0f - t) + 0.20f * t;
        float g = 0.60f * (1.0f - t) + 0.17f * t;
        float b = 0.85f * (1.0f - t) + 0.14f * t;
        for (int x = 0; x < w; ++x) {
            size_t i = (static_cast<size_t>(y) * w + x) * 4;
            px[i + 0] = static_cast<unsigned char>(r * 255.0f);
            px[i + 1] = static_cast<unsigned char>(g * 255.0f);
            px[i + 2] = static_cast<unsigned char>(b * 255.0f);
            px[i + 3] = 255;
        }
    }
    return px;
}

} // namespace

EnvironmentMap::EnvironmentMap(const VulkanContext& ctx, VkDescriptorPool pool)
    : ctx(ctx), pool(pool) {

    std::array<VkDescriptorSetLayoutBinding, 2> b{};
    b[0].binding = 0;                       // environnement
    b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[0].descriptorCount = 1;
    b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    b[1].binding = 1;                       // carte d'ombres
    b[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[1].descriptorCount = 1;
    b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 2;
    info.pBindings = b.data();
    if (vkCreateDescriptorSetLayout(ctx.device, &info, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("EnvironmentMap : echec du descriptor set layout");
    }

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = pool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &layout;
    if (vkAllocateDescriptorSets(ctx.device, &alloc, &set) != VK_SUCCESS) {
        throw std::runtime_error("EnvironmentMap : echec de l'allocation du descriptor set");
    }

    auto sky = generateDefaultSky(64, 32);
    texture = std::make_unique<Texture>(ctx, sky.data(), 64, 32);
    writeSet();
}

EnvironmentMap::~EnvironmentMap() {
    if (layout) vkDestroyDescriptorSetLayout(ctx.device, layout, nullptr);
}

void EnvironmentMap::writeSet() {
    VkDescriptorImageInfo img{};
    img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    img.imageView = texture->getImageView();
    img.sampler   = texture->getSampler();

    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = set;
    w.dstBinding = 0;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.descriptorCount = 1;
    w.pImageInfo = &img;
    VkDescriptorImageInfo shadowInfo{};
    VkWriteDescriptorSet writes[2] = { w, {} };
    uint32_t count = 1;

    if (shadowView != VK_NULL_HANDLE) {
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowInfo.imageView = shadowView;
        shadowInfo.sampler   = shadowSampler;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = set;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &shadowInfo;
        count = 2;
    }

    vkUpdateDescriptorSets(ctx.device, count, writes, 0, nullptr);
}

void EnvironmentMap::setTexture(const std::string& p) {
    vkDeviceWaitIdle(ctx.device);   // le set peut etre en cours d'utilisation
    texture = std::make_unique<Texture>(ctx, p);
    path = p;
    writeSet();                     // meme set, nouvelle image : le handle ne change pas
}