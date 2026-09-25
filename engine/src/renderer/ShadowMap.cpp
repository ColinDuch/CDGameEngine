#include <engine/renderer/ShadowMap.hpp>
#include <engine/renderer/BufferUtils.hpp>
#include <engine/renderer/Vertex.hpp>
#include <engine/renderer/Skeleton.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <fstream>
#include <stdexcept>

namespace {

// lightSpace + model : exactement 128 octets, la limite garantie par la spec.
// Aucun descriptor set n'est donc necessaire pour la passe d'ombre.
struct ShadowPush {
    glm::mat4 lightSpace;
    glm::mat4 model;
};

constexpr VkFormat SHADOW_FORMAT = VK_FORMAT_D32_SFLOAT;

} // namespace

glm::mat4 ShadowMap::computeLightSpace(const glm::vec3& lightDirection,
                                       const glm::vec3& center, float radius) {
    glm::vec3 dir = glm::normalize(lightDirection);
    if (glm::length(lightDirection) < 0.0001f) dir = glm::vec3(0.0f, 0.0f, -1.0f);

    // On recule la "camera" du soleil pour englober toute la sphere.
    glm::vec3 eye = center - dir * (radius * 2.0f);

    // Monde Z-up : on evite un up colineaire a la direction.
    glm::vec3 up = (std::abs(dir.z) > 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                             : glm::vec3(0.0f, 0.0f, 1.0f);

    glm::mat4 view = glm::lookAt(eye, center, up);
    glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius, 0.0f, radius * 4.0f);
    proj[1][1] *= -1;   // convention Vulkan, comme la camera principale

    return proj * view;
}

ShadowMap::ShadowMap(const VulkanContext& ctx, uint32_t size)
    : ctx(ctx), size(size) {
    createImage();
    createRenderPass();
    createFramebuffer();
    createPipeline();
}

ShadowMap::~ShadowMap() {
    if (skinnedPipeline) vkDestroyPipeline(ctx.device, skinnedPipeline, nullptr);
    if (skinnedLayout) vkDestroyPipelineLayout(ctx.device, skinnedLayout, nullptr);
    if (pipeline) vkDestroyPipeline(ctx.device, pipeline, nullptr);
    if (pipelineLayout) vkDestroyPipelineLayout(ctx.device, pipelineLayout, nullptr);
    if (framebuffer) vkDestroyFramebuffer(ctx.device, framebuffer, nullptr);
    if (renderPass) vkDestroyRenderPass(ctx.device, renderPass, nullptr);
    if (sampler) vkDestroySampler(ctx.device, sampler, nullptr);
    if (imageView) vkDestroyImageView(ctx.device, imageView, nullptr);
    if (image) vkDestroyImage(ctx.device, image, nullptr);
    if (memory) vkFreeMemory(ctx.device, memory, nullptr);
}

void ShadowMap::createImage() {
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = { size, size, 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.format = SHADOW_FORMAT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(ctx.device, &info, nullptr, &image) != VK_SUCCESS)
        throw std::runtime_error("ShadowMap : echec de l'image");

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(ctx.device, image, &req);

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = BufferUtils::findMemoryType(
        ctx.physicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(ctx.device, &alloc, nullptr, &memory) != VK_SUCCESS)
        throw std::runtime_error("ShadowMap : echec de l'allocation");
    vkBindImageMemory(ctx.device, image, memory, 0);

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = SHADOW_FORMAT;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    if (vkCreateImageView(ctx.device, &view, nullptr, &imageView) != VK_SUCCESS)
        throw std::runtime_error("ShadowMap : echec de l'image view");

    // Bordure BLANCHE : hors de la zone couverte, rien n'est dans l'ombre.
    VkSamplerCreateInfo samp{};
    samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samp.magFilter = VK_FILTER_LINEAR;
    samp.minFilter = VK_FILTER_LINEAR;
    samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samp.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samp.maxLod = 1.0f;
    if (vkCreateSampler(ctx.device, &samp, nullptr, &sampler) != VK_SUCCESS)
        throw std::runtime_error("ShadowMap : echec du sampler");
}

void ShadowMap::createRenderPass() {
    VkAttachmentDescription depth{};
    depth.format = SHADOW_FORMAT;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;      // on garde la profondeur
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference ref{};
    ref.attachment = 0;
    ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;                   // profondeur seule
    subpass.pDepthStencilAttachment = &ref;

    // La passe principale lira cette image : on synchronise la transition.
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &depth;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<uint32_t>(deps.size());
    info.pDependencies = deps.data();

    if (vkCreateRenderPass(ctx.device, &info, nullptr, &renderPass) != VK_SUCCESS)
        throw std::runtime_error("ShadowMap : echec du render pass");
}

void ShadowMap::createFramebuffer() {
    VkFramebufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = renderPass;
    info.attachmentCount = 1;
    info.pAttachments = &imageView;
    info.width = size;
    info.height = size;
    info.layers = 1;

    if (vkCreateFramebuffer(ctx.device, &info, nullptr, &framebuffer) != VK_SUCCESS)
        throw std::runtime_error("ShadowMap : echec du framebuffer");
}

std::vector<char> ShadowMap::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("ShadowMap : fichier introuvable : " + filename);
    size_t s = static_cast<size_t>(file.tellg());
    std::vector<char> buf(s);
    file.seekg(0);
    file.read(buf.data(), s);
    return buf;
}

VkShaderModule ShadowMap::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m;
    if (vkCreateShaderModule(ctx.device, &info, nullptr, &m) != VK_SUCCESS)
        throw std::runtime_error("ShadowMap : echec du shader module");
    return m;
}

void ShadowMap::createPipelineInternal(const char* vertPath, VkDescriptorSetLayout boneLayout,
                                       VkPipeline& outPipeline, VkPipelineLayout& outLayout) {
    VkShaderModule vs = createShaderModule(readFile(vertPath));

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage.module = vs;
    stage.pName = "main";

    const bool skinned =(boneLayout != VK_NULL_HANDLE);

    auto binding = Vertex::getBindingDescription();
    auto attributes = Vertex::getAttributeDescriptions();
    auto skinBinding = VertexSkin::bindingDescription();
    auto skinAttrs = VertexSkin::attributeDescriptions();

    std::vector<VkVertexInputBindingDescription> bindings{ binding };
    std::vector<VkVertexInputAttributeDescription> attrs(attributes.begin(), attributes.end());
    if (skinned) {
        bindings.push_back(skinBinding);
        attrs.insert(attrs.end(), skinAttrs.begin(), skinAttrs.end());
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{};
    vp.width = static_cast<float>(size);
    vp.height = static_cast<float>(size);
    vp.maxDepth = 1.0f;
    VkRect2D sc{};
    sc.extent = { size, size };

    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.pViewports = &vp;
    vpState.scissorCount = 1;
    vpState.pScissors = &sc;

    // Biais de profondeur : evite l'acne d'ombre (auto-ombrage en damier).
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    rs.depthBiasEnable = VK_TRUE;
    rs.depthBiasConstantFactor = 1.5f;
    rs.depthBiasSlopeFactor = 2.5f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 0;

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(ShadowPush);

    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = (boneLayout != VK_NULL_HANDLE) ? 1u : 0u;
    pl.pSetLayouts = &boneLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(ctx.device, &pl, nullptr, &outLayout) != VK_SUCCESS)
        throw std::runtime_error("ShadowMap : echec du pipeline layout");

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 1;                 // vertex seul : pas de fragment shader
    info.pStages = &stage;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vpState;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &cb;
    info.layout = outLayout;
    info.renderPass = renderPass;
    info.subpass = 0;

    if (vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &info, nullptr, &outPipeline) != VK_SUCCESS)
        throw std::runtime_error("ShadowMap : echec du pipeline");

    vkDestroyShaderModule(ctx.device, vs, nullptr);
}

void ShadowMap::record(VkCommandBuffer cmd,
                       const std::vector<std::shared_ptr<SceneObject>>& objects,
                       const glm::mat4& lightSpace,
                       VkBuffer terrainVB, VkBuffer terrainIB, uint32_t terrainIndexCount,
                       const glm::mat4& terrainModel,
                       const std::vector<VkDescriptorSet>& boneSets) {
    VkClearValue clear{};
    clear.depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = renderPass;
    begin.framebuffer = framebuffer;
    begin.renderArea.extent = { size, size };
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;

    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);

    ShadowPush push;
    push.lightSpace = lightSpace;

    for (size_t i = 0; i < objects.size(); ++i) {
        const auto& object = objects[i];

        // cet objet a-t-il de quoi etre skinne ?
        const bool skinned = (skinnedPipeline != VK_NULL_HANDLE)
                           && (i < boneSets.size()) && (boneSets[i] != VK_NULL_HANDLE)
                           && (object->getSkinBufferHandle() != VK_NULL_HANDLE);
        
        /*
        chaque pipeline a son layout
        les push constants et les descriptor sets
        doivent utiliser celui du pipeline lier
        */
        VkPipeline pipe = skinned ? skinnedPipeline : pipeline;
        VkPipelineLayout lay = skinned ? skinnedLayout : pipelineLayout;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        
        if (skinned) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lay, 0, 1, &boneSets[i], 0, nullptr);

            VkBuffer vbs[] = { object->getVertexBufferHandle(), object-> getSkinBufferHandle() };
            VkDeviceSize offs[] = { 0, 0 };
            vkCmdBindVertexBuffers(cmd, 0, 2, vbs, offs); 
        } else {
            VkBuffer vbs[] = { object->getVertexBufferHandle() };
            VkDeviceSize offs[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offs);
        }

        push.model = object->getModelMatrix();
        vkCmdPushConstants(cmd, lay, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPush), &push);
        
        vkCmdBindIndexBuffer(cmd, object->getIndexBufferHandle(), 0, VK_INDEX_TYPE_UINT32);

        for (const auto& sub : object->getSubMeshes()) {
            vkCmdDrawIndexed(cmd, sub.indexCount, 1, sub.indexOffset, 0, 0);
        }
    }

    if (terrainIndexCount > 0 && terrainVB != VK_NULL_HANDLE) {
        // les terrain n'est jamais skinne don con remet la pipeline de base 
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline); 

        push.model = terrainModel;
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPush), &push);

        VkBuffer tvbs[] = { terrainVB }; 
        VkDeviceSize toffs[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, tvbs, toffs);
        vkCmdBindIndexBuffer(cmd, terrainIB, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, terrainIndexCount, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void ShadowMap::createPipeline() {
    createPipelineInternal("shaders/shadow_vert.spv", VK_NULL_HANDLE, pipeline, pipelineLayout);
}

void ShadowMap::createSkinnedPipeline(VkDescriptorSetLayout boneLayout) {
    if (boneLayout == VK_NULL_HANDLE)
        throw std::runtime_error("ShadowMap : createSkinnedPipeline appels sans boneLayout");
    createPipelineInternal("shaders/shadow_skinned_vert.spv", boneLayout, skinnedPipeline, skinnedLayout);
}
