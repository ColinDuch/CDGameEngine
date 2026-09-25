#include <engine/core/Application.hpp>
#include <engine/core/Config.hpp>
#include <engine/core/Localization.hpp>
#include <engine/core/UiHelpers.hpp>

#include <engine/renderer/UniformBufferObject.hpp>
#include <engine/renderer/GriGeometry.hpp>
#include <engine/renderer/CheckerTexture.hpp>
#include <engine/renderer/DefaultNormal.hpp>
#include <engine/renderer/Frustum.hpp>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <chrono>
#include <ImGuiFileDialog.h>
#include <ImGuizmo.h>
#include <imgui.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <algorithm>
#include <functional>

Application::Application() {
    initWindow();
    initVulkanCore();
    initSwapchainResources();
    initRenderResources();
    initScene();
    initCommandsAndSync();
}

Application::~Application() {
    vkDeviceWaitIdle(vulkanDevice->getHandle());

    if (materialLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice->getHandle(), materialLayout, nullptr);
        materialLayout = VK_NULL_HANDLE;
    }

    bonePool.clear();
    if (boneLayout) vkDestroyDescriptorSetLayout(vulkanDevice->getHandle(), boneLayout, nullptr);

}

void Application::run() {
    loadPrefs();
    // Projet passe en argument : on l'ouvre directement.
    if (!startupProject.empty()) {
        namespace fs = std::filesystem;
        fs::path p(startupProject);
        if (fs::exists(p / "scene.json")) {
            currentProjectDir = fs::absolute(p).string();
            loadScene();
        } else {
            sceneMessage = "Projet invalide (scene.json introuvable) : " + startupProject;
        }
    }

    while (!quitting) {
        window->pollEvents();

        input.update(window->getHandle());

        // Demande de fermeture (croix ou Echap) : on l'annule et on demande confirmation.
        if (window->shouldClose() || window->shouldExit()) {
            window->cancelClose();
            askQuit = true;
        }

        drawFrame();
    }
}

void Application::initWindow() {
    window = std::make_unique<Window>(Config::WindowWidth, Config::WindowHeight, Config::WindowTitle);
    camera = std::make_unique<Camera>(glm::vec3(2.0f, 2.0f, 2.0f));
}

void Application::initVulkanCore() {
    auto requiredExtensions = Window::getRequiredVulkanExtensions();
    vulkanInstance = std::make_unique<VulkanInstance>(requiredExtensions);

    surface = std::make_unique<Surface>(vulkanInstance->getHandle(), window->getHandle());

    vulkanDevice = std::make_unique<VulkanDevice>(vulkanInstance->getHandle(), surface->getHandle());
    vulkanContext.physicalDevice = vulkanDevice->getPhysicalDevice();
    vulkanContext.device = vulkanDevice->getHandle();
    vulkanContext.graphicsQueue = vulkanDevice->getGraphicsQueue();
}

void Application::initSwapchainResources() {
    swapchain = std::make_unique<Swapchain>(
        vulkanDevice->getPhysicalDevice(), vulkanDevice->getHandle(),
        surface->getHandle(), window->getHandle(), vulkanDevice->getQueueFamilyIndices()
    );

    depthResources = std::make_unique<DepthResources>(
        vulkanContext, swapchain->getExtent()
    );

    renderPass = std::make_unique<RenderPass>(
        vulkanDevice->getHandle(), swapchain->getImageFormat(), depthResources->getFormat()
    );
}

void Application::initRenderResources() {
    uniformBuffer = std::make_unique<UniformBuffer>(vulkanContext);

    commandPool = std::make_unique<CommandPool>(
        vulkanDevice->getHandle(), vulkanDevice->getQueueFamilyIndices().graphicsFamily.value()
    );

    vulkanContext.commandPool = commandPool->getHandle();

    imguiLayer = std::make_unique<ImGuiLayer>(
        window->getHandle(), vulkanInstance->getHandle(), vulkanDevice->getPhysicalDevice(),
        vulkanDevice->getHandle(), vulkanDevice->getQueueFamilyIndices().graphicsFamily.value(),
        vulkanDevice->getGraphicsQueue(), renderPass->getHandle(),
        static_cast<uint32_t>(swapchain->getImageViews().size()), commandPool->getHandle()
    );

    descriptorSetLayout = std::make_unique<DescriptorSetLayout>(vulkanDevice->getHandle());
    descriptorPool = std::make_unique<DescriptorPool>(vulkanDevice->getHandle(), Config::DescriptorPoolCapacity);
    environment = std::make_unique<EnvironmentMap>(vulkanContext, descriptorPool->getHandle());
    shadowMap = std::make_unique<ShadowMap>(vulkanContext, 2048);
    environment->setShadowMap(shadowMap->getImageView(), shadowMap->getSampler());

    // Materiaux : le layout doit exister AVANT la creation des pipelines.
    materialLayout = MaterialAsset::createLayout(vulkanDevice->getHandle());
    defaultMaterial = std::make_unique<MaterialAsset>(
        vulkanContext, descriptorPool->getHandle(), materialLayout, "Default");

    skyRenderer = std::make_unique<SkyRenderer>(
        vulkanDevice->getHandle(), renderPass->getHandle(), environment->getLayout());

    boneLayout = BoneBuffer::createLayout(vulkanDevice->getHandle());
    shadowMap->createSkinnedPipeline(boneLayout);

    pipeline = std::make_unique<Pipeline>(
        vulkanDevice->getHandle(), renderPass->getHandle(),
        swapchain->getExtent(), descriptorSetLayout->getHandle(),
        environment->getLayout(), materialLayout
    );

    transparentPipeline = std::make_unique<Pipeline>(
        vulkanDevice->getHandle(), renderPass->getHandle(),
        swapchain->getExtent(), descriptorSetLayout->getHandle(),
        environment->getLayout(), materialLayout,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        "shaders/vert.spv", "shaders/frag.spv",
        1, true
    );

    skinnedPipeline = std::make_unique<Pipeline>(
        vulkanDevice->getHandle(), renderPass->getHandle(), swapchain->getExtent(),
        descriptorSetLayout->getHandle(),
        environment->getLayout(),
        materialLayout,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        "shaders/skinned_vert.spv", "shaders/frag.spv",
        1,
        false,
        boneLayout
    );

    auto gridVerts = generateGridVertices(10, 1.0f);
    gridVertexCount = static_cast<uint32_t>(gridVerts.size());
    gridVertexBuffer = std::make_unique<VertexBuffer>(vulkanContext, gridVerts);

    gridPipeline = std::make_unique<Pipeline>(
        vulkanDevice->getHandle(), renderPass->getHandle(), swapchain->getExtent(),
        descriptorSetLayout->getHandle(),
        environment->getLayout(), materialLayout,
        VK_PRIMITIVE_TOPOLOGY_LINE_LIST
    );

    terrain = std::make_unique<Terrain>(
        vulkanContext,
        descriptorPool->getHandle(), descriptorSetLayout->getHandle(),
        uniformBuffer->getHandle(),
        terrainResX, terrainResY, terrainSizeX, terrainSizeY
    );

    // Le terrain a son propre shader qui n'utilise PAS le set 2 : d'ou VK_NULL_HANDLE.
    terrainPipeline = std::make_unique<Pipeline>(
        vulkanDevice->getHandle(), renderPass->getHandle(), swapchain->getExtent(),
        terrain->getLayerLayout(), environment->getLayout(),
        materialLayout,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        "shaders/terrain_vert.spv", "shaders/terrain_frag.spv",
        4);

    // Descriptor set dedie a la grille : UBO (binding 0) + textures bidon.
    // Jamais echantillonnees (mode 3 sort avant), mais Vulkan veut des samplers valides.
    {
        auto checker = generateCheckerboard(256, 8);
        gridDummyTexture = std::make_unique<Texture>(vulkanContext, checker.data(), 256, 256);
        auto flat = generateFlatNormal();
        gridDummyNormal = std::make_unique<Texture>(vulkanContext, flat.data(), 2, 2);
        gridDescriptorSet = std::make_unique<DescriptorSet>(
            vulkanDevice->getHandle(), descriptorPool->getHandle(), descriptorSetLayout->getHandle(),
            uniformBuffer->getHandle(),
            gridDummyTexture->getImageView(), gridDummyTexture->getSampler(),
            gridDummyNormal->getImageView(), gridDummyNormal->getSampler()
        );
    }

    framebuffer = std::make_unique<Framebuffer>(
        vulkanDevice->getHandle(), renderPass->getHandle(),
        swapchain->getImageViews(), depthResources->getImageView(), swapchain->getExtent()
    );
}

void Application::loadBuiltinMeshes() {

    // --- Primitives de blockout : damier gris, disponibles des le demarrage. ---
    struct PrimitiveDef { const char* file; const char* name; };
    static const PrimitiveDef kPrimitives[] = {
        { "assets/models/Primitive_Cube.fbx",     "Cube" },
        { "assets/models/Primitive_Sphere.fbx",   "Sphere" },
        { "assets/models/Primitive_Cylinder.fbx", "Cylinder" },
        { "assets/models/Primitive_Cone.fbx",     "Cone" },
        { "assets/models/Primitive_Pyramid.fbx",  "Pyramid" },
        { "assets/models/Primitive_Slope.fbx",    "Slope" },
        { "assets/models/Primitive_Plane.fbx",    "Plane" },
    };
    const std::string kBlockoutTex = "assets/textures/checker_blockout.png";

    for (const auto& p : kPrimitives) {
        try {
            auto mesh = std::make_shared<Mesh>(
                vulkanContext, p.file, kBlockoutTex,
                descriptorPool->getHandle(), descriptorSetLayout->getHandle(),
                uniformBuffer->getHandle()
            );
            mesh->setBuiltinName(p.name);
            availableMeshes.push_back(mesh);
            meshNames.push_back(p.name);
        } catch (const std::exception& e) {
            std::cerr << "Primitive ignoree : " << p.file << " (" << e.what() << ")" << std::endl;
        }
    }

    // --- Personnage rigge : PAS de texture -> il garde la couleur de son materiau. ---
    try {
        auto character = std::make_shared<Mesh>(
            vulkanContext, "assets/models/Primitive_Character.fbx", "",
            descriptorPool->getHandle(), descriptorSetLayout->getHandle(),
            uniformBuffer->getHandle()
        );
        character->setBuiltinName("Character");
        availableMeshes.push_back(character);
        meshNames.push_back("Character");
    } catch (const std::exception& e) {
        std::cerr << "Personnage ignore : " << e.what() << std::endl;
    }
    for (size_t i= 0; i < availableMeshes.size(); ++i) {
        std::cout <<"[lib] " <<i << " : "
                  << (i < meshNames.size() ? meshNames[i] : std::string("<NO NAME"))
                  << (availableMeshes[i]->isSkinned() ? "   [skinne]" : "") << std::endl;
    }
    std::cout <<"[lib] " <<availableMeshes.size() << " meshes / " << meshNames.size() << " names" << std::endl;
    builtinMeshCount = availableMeshes.size();

}

void Application::initScene() {

    loadBuiltinMeshes();

    // Un objet au centre pour ne pas demarrer sur une scene vide.
    if (!availableMeshes.empty()) {
        auto obj = std::make_shared<SceneObject>(availableMeshes[0]);
        obj->position = glm::vec3(0.0f);
        obj->meshIndex = 0;
        obj->name = meshNames[0];
        sceneObjects.push_back(obj);
    }

    scripts = std::make_unique<ScriptEngine>(sceneObjects);
    scripts->setWindow(window->getHandle());

    orbitDistance = glm::length(camera->getPosition() - orbitTarget);

    {
        std::vector<Vertex> sv;
        std::vector<uint32_t> si;
        generateSphere(sv, si, 64, 32, 1.0f);
        previewIndexCount = static_cast<uint32_t>(si.size());
        previewSphere = std::make_shared<Mesh>(
            vulkanContext, sv, si, std::string{}, std::string{},
            descriptorPool->getHandle(), descriptorSetLayout->getHandle(),
            uniformBuffer->getHandle());
    }
}

void Application::initCommandsAndSync() {
    commandBuffer = std::make_unique<CommandBuffer>(
        vulkanDevice->getHandle(), commandPool->getHandle(),
        renderPass->getHandle(), pipeline->getHandle(),
        framebuffer->getHandles(), swapchain->getExtent(),
        pipeline->getLayout(),
        sceneObjects
    );

    commandBuffer->setMaterials({}, defaultMaterial->getSet());
    commandBuffer->setTransparentPipeline(transparentPipeline->getHandle());

    commandBuffer->setTerrainPipeline(
        terrainPipeline->getHandle(), terrainPipeline->getLayout());

    commandBuffer->setTerrain(
        terrain->getVertexBufferHandle(), terrain->getIndexBufferHandle(),
        terrain->getIndexCount(), terrain->getLayerSet(),terrain->getShadowIndexBufferHandle(), terrain->getShadowIndexCount());
    
    commandBuffer->setEnvironment(environment->getSet());
    commandBuffer->setSky(skyRenderer.get(), showSky);

    commandBuffer->setGrid(
        gridPipeline->getHandle(),
        gridVertexBuffer->getHandle(),
        gridVertexCount,
        gridDescriptorSet->getHandle()
    );

    syncObjects = std::make_unique<SyncObjects>(
        vulkanDevice->getHandle(), static_cast<uint32_t>(swapchain->getImageViews().size())
    );

}

float Application::calculateDeltaTime() {
    static auto lastFrameTime = std::chrono::high_resolution_clock::now();
    auto currentFrameTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float>(currentFrameTime - lastFrameTime).count();
    lastFrameTime = currentFrameTime;
    return deltaTime;
}

void Application::updateUniformBuffer() {
    UniformBufferObject ubo{};
    ubo.view = camera->getViewMatrix();

    VkExtent2D extent = swapchain->getExtent();
    ubo.proj = glm::perspective(glm::radians(camera->getFov()), extent.width / (float)extent.height, cameraNear, cameraFar);
    ubo.proj[1][1] *= -1;

    projectionMatrix = ubo.proj;

    ubo.lightDirection = glm::normalize(lightDirection);
    ubo.lightColor = lightColor;
    ubo.lightIntensity = lightIntensity;
    ubo.ambientStrength = ambientStrength;
    ubo.cameraPosition = camera->getPosition();
    ubo.shininess = shininess;
    ubo.exposure = exposure;
    ubo.lightSpace = lightSpaceMatrix;
    // Aucune passe d'ombre n'est enregistree en apercu de materiau : la shadow map
    // contient encore la scene. On projette hors de sa zone pour que
    // shadowFactor() renvoie 1.0 partout.
    if (editorMode == EditorMode::Material) {
        ubo.lightSpace = glm::translate(glm::mat4(1.0f), glm::vec3(1.0e6f));
    }
    ubo.shadowStrength = shadowStrength;

    uniformBuffer->update(ubo);

}

void Application::regenerateGrid() {
    auto gridVerts = generateGridVertices(gridHalfSize, gridStep);
    gridVertexCount = static_cast<uint32_t>(gridVerts.size());

    vkDeviceWaitIdle(vulkanDevice->getHandle());
    gridVertexBuffer = std::make_unique<VertexBuffer>(vulkanContext, gridVerts);

    commandBuffer->setGrid(
        gridPipeline->getHandle(),
        gridVertexBuffer->getHandle(),
        gridVertexCount,
        gridDescriptorSet->getHandle()
    );
}
void Application::drawFrame() {
    float deltaTime = calculateDeltaTime();
    updateCamera(deltaTime);
    if (isPlaying) updateGameplay(deltaTime);
    updateAnimations(deltaTime);
    updateUniformBuffer();

    VkDevice device = vulkanDevice->getHandle();
    VkFence fence = syncObjects->getInFlightFence();

    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapchain->getHandle(), UINT64_MAX,
        syncObjects->getImageAvailableSemaphore(), VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    vkResetFences(device, 1, &fence);

    buildUI();

    bool preview = (editorMode == EditorMode::Material);
    VkDescriptorSet matSet = defaultMaterial->getSet();
    if (selectedMaterial >= 0 && selectedMaterial < static_cast<int>(materials.size()))
        matSet = materials[selectedMaterial]->getSet();

    commandBuffer->setPreview(preview,
        previewSphere->getVertexBufferHandle(), previewSphere->getIndexBufferHandle(),
        previewIndexCount, previewSphere->getDescriptorSetHandle(), matSet);

    bool previewTransp = false;
    if (selectedMaterial >= 0 && selectedMaterial < static_cast<int>(materials.size())) {
        previewTransp = materials[selectedMaterial]->params().baseColor.a < 0.999f
                     || materials[selectedMaterial]->hasMap(MAP_OPACITY);
    }
    commandBuffer->setPreviewTransparent(previewTransp);

    commandBuffer->recordFrame(imageIndex, [this](VkCommandBuffer cmd) {
        imguiLayer->render(cmd);
    });

    VkSemaphore waitSemaphores[] = { syncObjects->getImageAvailableSemaphore() };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { syncObjects->getRenderFinishedSemaphore(imageIndex) };

    VkCommandBuffer cmdBuffer = commandBuffer->getHandles()[imageIndex];

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(vulkanDevice->getGraphicsQueue(), 1, &submitInfo, fence) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkSwapchainKHR swapchains[] = { swapchain->getHandle() };

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(vulkanDevice->getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window->wasResized()) {
        window->resetResizedFlag();
        recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }
}

void Application::recreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window->getHandle(), &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window->getHandle(), &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(vulkanDevice->getHandle());

    swapchain->recreate();
    depthResources->recreate(swapchain->getExtent());
    framebuffer->recreate(renderPass->getHandle(), swapchain->getImageViews(), depthResources->getImageView(), swapchain->getExtent());
    commandBuffer->updateFramebuffers(framebuffer->getHandles(), swapchain->getExtent());
}
