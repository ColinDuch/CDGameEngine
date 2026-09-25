#include <engine/renderer/CommandBuffer.hpp>
#include <engine/renderer/VertexBuffer.hpp>
#include <engine/renderer/IndexBuffer.hpp>
#include <engine/renderer/PushConstants.hpp>
#include <glm/glm.hpp>
#include <stdexcept>
#include <array>

CommandBuffer::CommandBuffer(VkDevice device, VkCommandPool commandPool,              VkRenderPass renderPass, VkPipeline pipeline,
          const std::vector<VkFramebuffer>& framebuffers,
          VkExtent2D extent, VkPipelineLayout pipelineLayout,
          const std::vector<std::shared_ptr<SceneObject>>& sceneObjects)
    : device(device), renderPass(renderPass), pipeline(pipeline),
    framebuffers(framebuffers), extent(extent),
    pipelineLayout(pipelineLayout), sceneObjects(sceneObjects) {

    commandBuffers.resize(framebuffers.size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void CommandBuffer::recordFrame(uint32_t imageIndex, const std::function<void(VkCommandBuffer)>& imguiRenderCallback) {
    VkCommandBuffer cmd = commandBuffers[imageIndex];

    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    // Passe d'ombre : AVANT le render pass principal (une passe ne peut pas en contenir une autre).
    if (shadow && !previewMode) {
        // On dessine le terrain avec sa grille ALLEGEE : la shadow map ne fait
        // que 2048², elle ne voit pas la difference.
        bool haveLight = (shadowTerrainIndexBuffer != VK_NULL_HANDLE);
        VkBuffer sib = haveLight ? shadowTerrainIndexBuffer : terrainIndexBuffer;
        uint32_t sic = haveLight ? shadowTerrainIndexCount  : terrainIndexCount;

        shadow->record(cmd, sceneObjects, shadowLightSpace,
                       terrainVertexBuffer, sib,
                       terrainVisible ? sic : 0, terrainModel, boneSets);
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    // Sets globaux du pipeline principal : 1 = environnement, 2 = materiau par defaut.
    // Vulkan exige que TOUT set utilise par le shader soit lie, meme si l'objet
    // n'a pas de materiau : d'ou le set par defaut.
    auto bindGlobals = [&](VkPipelineLayout lay) {
        if (envDescriptorSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                lay, 1, 1, &envDescriptorSet, 0, nullptr);
        }
        if (defaultMaterialSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                lay, 2, 1, &defaultMaterialSet, 0, nullptr);
        }
    };

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (sky && skyVisible) {
        sky->record(cmd, envDescriptorSet, skyInvViewProj, extent, skyExposure);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    bindGlobals(pipelineLayout);;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // --- Mode apercu de materiau : uniquement le ciel + une sphere ---
    if (previewMode && previewCount > 0 && previewMeshSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout, 0, 1, &previewMeshSet, 0, nullptr);
        if (previewMatSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout, 2, 1, &previewMatSet, 0, nullptr);
        }
        
        if (previewTransparent && transparentPipeline != VK_NULL_HANDLE) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transparentPipeline);
            bindGlobals(pipelineLayout);;
        }

        PushConstants pp;
        pp.model = glm::mat4(1.0f);
        pp.renderMode = 7;
        pp.hasNormalMap = 0;
        pp.baseColor = glm::vec4(1.0f);
        pp.metallic = 0.0f;
        pp.roughness = 0.5f;

        vkCmdPushConstants(cmd, pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(PushConstants), &pp);

        VkBuffer pvbs[] = { previewVB };
        VkDeviceSize poffs[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, pvbs, poffs);
        vkCmdBindIndexBuffer(cmd, previewIB, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, previewCount, 1, 0, 0, 0);
    }

    // --- Dessin de la grille (pipeline lignes, avant les objets) ---
    if (!previewMode && gridVisible && gridPipeline != VK_NULL_HANDLE && gridVertexCount > 0 && gridDescriptorSet != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gridPipeline);
        bindGlobals(pipelineLayout);;

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout, 0, 1, &gridDescriptorSet, 0, nullptr);

        PushConstants gridPush;
        gridPush.model = glm::mat4(1.0f);
        gridPush.renderMode = 3;
        gridPush.hasNormalMap = 0;

        vkCmdPushConstants(cmd, pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(PushConstants), &gridPush);

        VkBuffer vbs[] = { gridVertexBuffer };
        VkDeviceSize offs[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offs);

        vkCmdDraw(cmd, gridVertexCount, 1, 0, 0);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    bindGlobals(pipelineLayout);;

    // --- Dessin du terrain (pipeline dedie, 4 couches de materiaux) ---
    // Le pipeline du terrain n'a que 2 sets (0 = couches, 1 = environnement) :
    // son shader n'utilise pas le set 2, donc rien a lier de plus ici.
    if (!previewMode && terrainVisible && terrainIndexCount > 0 && terrainPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeline);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            terrainPipelineLayout, 0, 1, &terrainDescriptorSet, 0, nullptr);
        if (envDescriptorSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                terrainPipelineLayout, 1, 1, &envDescriptorSet, 0, nullptr);
        }

        // Les 4 couches du sol = 4 materiaux, sets 2 a 5.
        for (uint32_t i = 0; i < 4; ++i) {
            if (terrainLayerSets[i] != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    terrainPipelineLayout, 2 + i, 1, &terrainLayerSets[i], 0, nullptr);
            }
        }

        PushConstants terrainPush;
        terrainPush.model = terrainModel;
        terrainPush.renderMode = terrainRenderMode;
        terrainPush.hasNormalMap = 0;
        terrainPush.baseColor = terrainLayers;
        terrainPush.metallic  = terrainMetallic;
        terrainPush.roughness = terrainRoughness;

        vkCmdPushConstants(cmd, terrainPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(PushConstants), &terrainPush);

        VkBuffer tvbs[] = { terrainVertexBuffer };
        VkDeviceSize toffs[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, tvbs, toffs);
        vkCmdBindIndexBuffer(cmd, terrainIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, terrainIndexCount, 1, 0, 0, 0);

        // Le terrain a change de pipeline : on rebinde le principal pour les objets.
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        bindGlobals(pipelineLayout);;
    }

    // Dessin d'un objet par son index (utilise deux fois : opaque puis transparent).
    auto drawObject = [&](int idx) {
        if (idx < 0 || idx >= static_cast<int>(sceneObjects.size())) return;
        const auto& object = sceneObjects[idx];

        // Cet objet est-il skinne ? Il lui faut un buffer de poids ET un set d'os.
        const bool skinned =
            skinnedPipeline != VK_NULL_HANDLE &&
            idx < static_cast<int>(boneSets.size()) && boneSets[idx] != VK_NULL_HANDLE &&
            object->getSkinBufferHandle() != VK_NULL_HANDLE;

        // Le pipeline skinne a son PROPRE layout (4 sets) : tout ce qui suit
        // doit utiliser celui-la, d'ou la variable `lay`.
        VkPipelineLayout lay = skinned ? skinnedLayout : pipelineLayout;

        if (skinned) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedPipeline);
            bindGlobals(skinnedLayout);

            // set 3 : les matrices d'os.
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                skinnedLayout, 3, 1, &boneSets[idx], 0, nullptr);

            // DEUX buffers de sommets : geometrie (binding 0) + poids (binding 1).
            VkBuffer vbs[] = { object->getVertexBufferHandle(), object->getSkinBufferHandle() };
            VkDeviceSize offs[] = { 0, 0 };
            vkCmdBindVertexBuffers(cmd, 0, 2, vbs, offs);
        } else {
            VkBuffer vertexBuffers[] = { object->getVertexBufferHandle() };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        }

        vkCmdBindIndexBuffer(cmd, object->getIndexBufferHandle(), 0, VK_INDEX_TYPE_UINT32);

        glm::mat4 model = object->getModelMatrix();

        int matIdx = object->materialIndex;
        bool useMaterial = (matIdx >= 0 && matIdx < static_cast<int>(materialSets.size())
                            && materialSets[matIdx] != VK_NULL_HANDLE);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lay, 2, 1,
            useMaterial ? &materialSets[matIdx] : &defaultMaterialSet, 0, nullptr);

        for (const auto& sub : object->getSubMeshes()) {
            VkDescriptorSet ds = sub.getDescriptorSetHandle();
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                lay, 0, 1, &ds, 0, nullptr);

            PushConstants pushData;
            pushData.model = model;
            pushData.renderMode = useMaterial ? 7 : sub.renderMode;
            pushData.hasNormalMap = sub.hasNormalMap ? 1 : 0;
            pushData.baseColor = glm::vec4(sub.baseColor, 1.0f);
            pushData.metallic  = object->metallic;
            pushData.roughness = object->roughness;
            pushData.worldUvScale = object->worldUvScale;

            vkCmdPushConstants(cmd, lay,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(PushConstants), &pushData);

            vkCmdDrawIndexed(cmd, sub.indexCount, 1, sub.indexOffset, 0, 0);
        }

        // On a change de pipeline pour cet objet : on rend le principal au suivant.
        if (skinned) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            bindGlobals(pipelineLayout);
        }
    };

    if (!previewMode) {
        // Opaques d'abord.
        if (opaqueOrder.empty() && transparentOrder.empty()) {
            for (int i = 0; i < static_cast<int>(sceneObjects.size()); ++i) drawObject(i);
        } else {
            for (int idx : opaqueOrder) drawObject(idx);

            // Puis les transparents, tries de l'arriere vers l'avant.
            if (!transparentOrder.empty() && transparentPipeline != VK_NULL_HANDLE) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transparentPipeline);
                bindGlobals(pipelineLayout);;
                for (int idx : transparentOrder) drawObject(idx);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                bindGlobals(pipelineLayout);;
            }
        }
    }

    if (imguiRenderCallback) {
        imguiRenderCallback(cmd);
    }

    vkCmdEndRenderPass(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void CommandBuffer::updateFramebuffers(const std::vector<VkFramebuffer>& newFramebuffers, VkExtent2D newExtent) {
    framebuffers = newFramebuffers;
    extent = newExtent;
}

void CommandBuffer::setSceneObjects(const std::vector<std::shared_ptr<SceneObject>>& objects) {
    sceneObjects = objects;
}

void CommandBuffer::setGrid(VkPipeline gp, VkBuffer gvb, uint32_t count, VkDescriptorSet ds) {
    gridPipeline = gp;
    gridVertexBuffer = gvb;
    gridVertexCount = count;
    gridDescriptorSet = ds;
}

void CommandBuffer::setTerrain(VkBuffer terrainVertexBuffer, VkBuffer terrainIndexBuffer,
                               uint32_t terrainIndexCount, VkDescriptorSet terrainDescriptorSet,
                               VkBuffer shadowIndexBuffer, uint32_t shadowIndexCount) {
    this->terrainVertexBuffer = terrainVertexBuffer;
    this->terrainIndexBuffer = terrainIndexBuffer;
    this->terrainIndexCount = terrainIndexCount;
    this->terrainDescriptorSet = terrainDescriptorSet;
    this->shadowTerrainIndexBuffer = shadowIndexBuffer;
    this->shadowTerrainIndexCount = shadowIndexCount;
}

void CommandBuffer::setMaterials(const std::vector<VkDescriptorSet>& sets, VkDescriptorSet defaultSet) {
    materialSets = sets;
    defaultMaterialSet = defaultSet;
}
