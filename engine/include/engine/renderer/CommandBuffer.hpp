#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>
#include <memory>
#include <functional>

#include <engine/renderer/SceneObject.hpp>
#include <engine/renderer/SkyRenderer.hpp>
#include <engine/renderer/ShadowMap.hpp>

class CommandBuffer {
public:
    CommandBuffer(VkDevice device, VkCommandPool commandPool,
              VkRenderPass renderPass, VkPipeline pipeline,
              const std::vector<VkFramebuffer>& framebuffers,
              VkExtent2D extent, VkPipelineLayout pipelineLayout,
              const std::vector<std::shared_ptr<SceneObject>>& sceneObjects);
    
    void recordFrame(uint32_t imageIndex, const std::function<void(VkCommandBuffer)>& imguiRenderCallback);
    void updateFramebuffers(const std::vector<VkFramebuffer>& newFramebuffers, VkExtent2D newExtent);
    void setSceneObjects(const std::vector<std::shared_ptr<SceneObject>>& objects);
    void setGrid(VkPipeline gridPipeline, VkBuffer gridVertexBuffer, uint32_t gridVertexCount, VkDescriptorSet gridDescriptorSet);
    void setGridVisible(bool visible) { gridVisible = visible; }
    void setTerrain(VkBuffer terrainVertexBuffer, VkBuffer terrainIndexBuffer,
                    uint32_t terrainIndexCount, VkDescriptorSet terrainDescriptorSet,
                    VkBuffer shadowIndexBuffer, uint32_t shadowIndexCount);
    void setTerrainVisible(bool visible) { terrainVisible = visible; }
    void setTerrainRenderMode(int mode) { terrainRenderMode = mode; }
    void setTerrainLayers(const glm::vec4& params) { terrainLayers = params; }
    void setTerrainModel(const glm::mat4& m) { terrainModel = m; }
    void setTerrainMaterial(float m, float r) { terrainMetallic = m; terrainRoughness = r; }
    void setEnvironment(VkDescriptorSet s) { envDescriptorSet = s; }
    void setSky(SkyRenderer* s, bool visible) { sky = s; skyVisible = visible; }
    void setSkyMatrix(const glm::mat4& m) { skyInvViewProj = m; }
    void setTerrainPipeline(VkPipeline p, VkPipelineLayout l) { terrainPipeline = p; terrainPipelineLayout = l; }
    void setSkyExposure(float e) { skyExposure = e; }
    void setShadow(ShadowMap* s, const glm::mat4& ls) { shadow = s; shadowLightSpace = ls; }
    void setMaterials(const std::vector<VkDescriptorSet>& sets, VkDescriptorSet defaultSet);
    void setTerrainLayerSets(const VkDescriptorSet* sets) {
        for (int i = 0; i < 4; ++i) terrainLayerSets[i] = sets[i];
    }

    void setTransparentPipeline(VkPipeline p) { transparentPipeline = p; }
    void setDrawOrder(const std::vector<int>& op, const std::vector<int>& tr) {
        opaqueOrder = op; transparentOrder = tr;
    }

    void setPreviewTransparent(bool t) { previewTransparent = t; }

    void setPreview(bool enabled, VkBuffer vb, VkBuffer ib, uint32_t count,
                    VkDescriptorSet meshSet, VkDescriptorSet matSet) {
        previewMode = enabled; previewVB = vb; previewIB = ib;
        previewCount = count; previewMeshSet = meshSet; previewMatSet = matSet;
    }

    void setSkinned(VkPipeline p, VkPipelineLayout l) { skinnedPipeline = p; skinnedLayout = l; }
    void setBoneSets(const std::vector<VkDescriptorSet>& sets) { boneSets = sets; }

    int getTerrainRenderMode() const { return terrainRenderMode; }

    VkPipeline terrainPipeline = VK_NULL_HANDLE;
    VkPipelineLayout terrainPipelineLayout = VK_NULL_HANDLE;

    const std::vector<VkCommandBuffer>& getHandles() const { return commandBuffers; }
    glm::mat4 terrainModel = glm::mat4(1.0f);

    float terrainMetallic  = 0.0f;
    float terrainRoughness = 0.85f;
    float skyExposure = 1.0f;
    ShadowMap* shadow = nullptr;
    glm::mat4 shadowLightSpace{1.0f};


private:
    VkDevice device;
    VkRenderPass renderPass;
    VkPipeline pipeline;
    std::vector<VkFramebuffer> framebuffers;
    VkExtent2D extent;
    VkPipelineLayout pipelineLayout;
    std::vector<std::shared_ptr<SceneObject>> sceneObjects;
    VkPipeline gridPipeline = VK_NULL_HANDLE;
    VkBuffer gridVertexBuffer = VK_NULL_HANDLE;
    uint32_t gridVertexCount = 0;
    bool gridVisible = true;
    VkDescriptorSet gridDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet envDescriptorSet = VK_NULL_HANDLE;

    VkBuffer terrainVertexBuffer = VK_NULL_HANDLE;
    VkBuffer terrainIndexBuffer = VK_NULL_HANDLE;
    uint32_t terrainIndexCount = 0;
    VkBuffer shadowTerrainIndexBuffer = VK_NULL_HANDLE;
    uint32_t shadowTerrainIndexCount = 0;
    VkDescriptorSet terrainDescriptorSet = VK_NULL_HANDLE;
    bool terrainVisible = true;
    int terrainRenderMode = 5;   // 5 = damier monde (defaut), 0 = texture
    
    glm::vec4 terrainLayers = glm::vec4(0.0f, 5.0f, 10.0f, 0.6f); // sable<, herbe<, roche<, seuil pente

    std::vector<VkCommandBuffer> commandBuffers;

    SkyRenderer* sky = nullptr;
    bool skyVisible = true;
    glm::mat4 skyInvViewProj{1.0f};

    std::vector<VkDescriptorSet> materialSets;
    VkDescriptorSet defaultMaterialSet = VK_NULL_HANDLE;

    bool previewMode = false;
    VkBuffer previewVB = VK_NULL_HANDLE;
    VkBuffer previewIB = VK_NULL_HANDLE;
    uint32_t previewCount = 0;
    VkDescriptorSet previewMeshSet = VK_NULL_HANDLE;
    VkDescriptorSet previewMatSet = VK_NULL_HANDLE;
    VkDescriptorSet terrainLayerSets[4] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };

    VkPipeline transparentPipeline = VK_NULL_HANDLE;
    std::vector<int> opaqueOrder;
    std::vector<int> transparentOrder;

    bool previewTransparent = false;

    VkPipeline skinnedPipeline = VK_NULL_HANDLE;
    VkPipelineLayout skinnedLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> boneSets;   // indexe par OBJET

};