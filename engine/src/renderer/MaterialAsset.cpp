#include <engine/renderer/MaterialAsset.hpp>
#include <engine/renderer/BufferUtils.hpp>

#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

const char* const MapSlotNames[MAP_COUNT] = {
    "Albedo", "Normal map", "Roughness", "Metallic",
    "Ambient occlusion", "Height / displacement", "Emissive", "Opacity"
};

namespace {

// Petite texture unie 2x2 : sert de valeur par defaut quand une carte est absente.
std::vector<unsigned char> solid(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    std::vector<unsigned char> px(2 * 2 * 4);
    for (int i = 0; i < 4; ++i) {
        px[i * 4 + 0] = r; px[i * 4 + 1] = g;
        px[i * 4 + 2] = b; px[i * 4 + 3] = a;
    }
    return px;
}

// Valeur neutre de chaque carte : celle qui ne change RIEN au rendu.
std::vector<unsigned char> defaultPixels(int slot) {
    switch (slot) {
        case MAP_NORMAL:   return solid(128, 128, 255, 255);   // normale plate (0,0,1)
        case MAP_HEIGHT:   return solid(0, 0, 0, 255);         // aucun relief
        case MAP_EMISSIVE: return solid(0, 0, 0, 255);         // aucune emission
        default:           return solid(255, 255, 255, 255);   // multiplicateur neutre
    }
}

} // namespace

VkDescriptorSetLayout MaterialAsset::createLayout(VkDevice device) {
    std::array<VkDescriptorSetLayoutBinding, 1 + MAP_COUNT> b{};

    // binding 0 : les facteurs du materiau
    b[0].binding = 0;
    b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b[0].descriptorCount = 1;
    b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // bindings 1..8 : les 8 cartes
    for (int i = 0; i < MAP_COUNT; ++i) {
        b[i + 1].binding = static_cast<uint32_t>(i + 1);
        b[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[i + 1].descriptorCount = 1;
        b[i + 1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(b.size());
    info.pBindings = b.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("MaterialAsset : echec du descriptor set layout");
    }
    return layout;
}

MaterialAsset::MaterialAsset(const VulkanContext& ctx, VkDescriptorPool pool,
                             VkDescriptorSetLayout layout, const std::string& name)
    : ctx(ctx), pool(pool), layout(layout), name(name) {

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = pool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &layout;
    if (vkAllocateDescriptorSets(ctx.device, &alloc, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("MaterialAsset : echec de l'allocation du descriptor set");
    }

    createDefaultMaps();
    createParamBuffer();
    writeSet();
    uploadParams();
}

MaterialAsset::~MaterialAsset() {
    if (paramMapped) vkUnmapMemory(ctx.device, paramMemory);
    if (paramBuffer) vkDestroyBuffer(ctx.device, paramBuffer, nullptr);
    if (paramMemory) vkFreeMemory(ctx.device, paramMemory, nullptr);
}

void MaterialAsset::createDefaultMaps() {
    for (int i = 0; i < MAP_COUNT; ++i) {
        auto px = defaultPixels(i);
        defaults[i] = std::make_unique<Texture>(ctx, px.data(), 2, 2);
        maps[i] = nullptr;   // on utilisera defaults[i] tant qu'aucune carte n'est chargee
    }
}

void MaterialAsset::createParamBuffer() {
    VkDeviceSize size = sizeof(MaterialParams);
    BufferUtils::createBuffer(
        ctx.physicalDevice, ctx.device, size,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        paramBuffer, paramMemory
    );
    vkMapMemory(ctx.device, paramMemory, 0, size, 0, &paramMapped);
}

void MaterialAsset::uploadParams() {
    if (paramMapped) std::memcpy(paramMapped, &paramData, sizeof(MaterialParams));
}

void MaterialAsset::writeSet() {
    VkDescriptorBufferInfo buf{};
    buf.buffer = paramBuffer;
    buf.offset = 0;
    buf.range  = sizeof(MaterialParams);

    std::array<VkDescriptorImageInfo, MAP_COUNT> imgs{};
    std::array<VkWriteDescriptorSet, 1 + MAP_COUNT> w{};

    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = descriptorSet;
    w[0].dstBinding = 0;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].descriptorCount = 1;
    w[0].pBufferInfo = &buf;

    for (int i = 0; i < MAP_COUNT; ++i) {
        const Texture* t = maps[i] ? maps[i].get() : defaults[i].get();
        imgs[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgs[i].imageView = t->getImageView();
        imgs[i].sampler   = t->getSampler();

        w[i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i + 1].dstSet = descriptorSet;
        w[i + 1].dstBinding = static_cast<uint32_t>(i + 1);
        w[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[i + 1].descriptorCount = 1;
        w[i + 1].pImageInfo = &imgs[i];
    }

    vkUpdateDescriptorSets(ctx.device, static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
}

void MaterialAsset::setMap(int slot, const std::string& path) {
    if (slot < 0 || slot >= MAP_COUNT) return;

    vkDeviceWaitIdle(ctx.device);          // le set peut etre en cours d'utilisation
    maps[slot] = std::make_unique<Texture>(ctx, path);
    mapPaths[slot] = path;
    paramData.mapFlags |= (1 << slot);

    writeSet();                            // meme set reecrit : le handle ne change pas
    uploadParams();
}

void MaterialAsset::clearMap(int slot) {
    if (slot < 0 || slot >= MAP_COUNT) return;

    vkDeviceWaitIdle(ctx.device);
    maps[slot].reset();
    mapPaths[slot].clear();
    paramData.mapFlags &= ~(1 << slot);

    writeSet();
    uploadParams();
}
