#pragma once

#include <engine/core/Window.hpp>
#include <engine/core/Localization.hpp>
#include <engine/core/InputMap.hpp>

#include <engine/renderer/VulkanInstance.hpp>
#include <engine/renderer/Surface.hpp>
#include <engine/renderer/VulkanDevice.hpp>
#include <engine/renderer/Swapchain.hpp>
#include <engine/renderer/DepthResources.hpp>
#include <engine/renderer/RenderPass.hpp>
#include <engine/renderer/Pipeline.hpp>
#include <engine/renderer/Framebuffer.hpp>
#include <engine/renderer/CommandPool.hpp>
#include <engine/renderer/CommandBuffer.hpp>
#include <engine/renderer/SyncObjects.hpp>
#include <engine/renderer/VertexBuffer.hpp>
#include <engine/renderer/IndexBuffer.hpp>
#include <engine/renderer/UniformBuffer.hpp>
#include <engine/renderer/DescriptorSet.hpp>
#include <engine/renderer/Model.hpp>
#include <engine/renderer/Texture.hpp>
#include <engine/renderer/Camera.hpp>
#include <engine/renderer/SceneObject.hpp>
#include <engine/renderer/DescriptorSetLayout.hpp>
#include <engine/renderer/DescriptorPool.hpp>
#include <engine/renderer/Mesh.hpp>
#include <engine/renderer/VulkanContext.hpp>
#include <engine/renderer/Terrain.hpp>
#include <engine/renderer/EnvironmentMap.hpp>
#include <engine/renderer/SkyRenderer.hpp>
#include <engine/renderer/MaterialAsset.hpp>
#include <engine/renderer/SphereGeometry.hpp>
#include <engine/renderer/ShadowMap.hpp>
#include <engine/renderer/BoneBuffer.hpp>

#include <engine/ui/ImGuiLayer.hpp>

#include <engine/scene/GltfExporter.hpp>
#include <engine/scene/SceneSerializer.hpp>

#include <engine/script/ScriptEngine.hpp>

#include <imgui.h>
#include <memory>
#include <vector>
#include <string>

class Application {
public:
    Application();
    ~Application();

    void run();
    void setStartupProject(const std::string& path) { startupProject = path; }

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    int selectedObjectIndex = -1;

    int languageIndex = 0;   // 0 = English, 1 = Francais
    

private:
    void drawFrame();
    void recreateSwapchain();

    void initWindow();
    void initVulkanCore();
    void initSwapchainResources();
    void initRenderResources();
    void initScene();
    void loadBuiltinMeshes();   // primitives de blockout, toujours disponibles
    void initCommandsAndSync();

    float calculateDeltaTime();
    void updateCamera(float deltaTime);
    void updateEditorCamera(float dx, float dy, float wheel);
    void updateUniformBuffer();
    void buildUI();
    void drawMenuBar();
    void drawAssetsPanel();
    void drawHierarchyPanel();
    void drawInspectorPanel();
    void drawSettingsPanel();
    void drawFileMenu();
    void drawGizmo();
    void saveScene();
    void loadScene();
    void clearScene();
    void exportScene(const std::string& path);
    void importScene(const std::string& path);
    void drawLightPanel();
    void importObjModel(const std::string& path);
    void drawFileDialogs();
    void drawFpsCounter();
    void drawTerrainPanel();
    void regenerateGrid();
    void drawScriptConsole();
    void drawStartupPanel();
    void drawAxisGizmo();
    bool showCameraPanel = false;
    void drawCameraPanel();
    bool showAnimationPanel = true;
    void drawAnimationPanel();

    void loadPrefs();
    void savePrefs();

    bool sunSelected = false;
    bool showSunGizmo = true;
    glm::vec3 sunGizmoPos{0.0f, 0.0f, 8.0f};   // ancrage visuel (n'affecte pas l'eclairage)
    void drawSunGizmo();

    bool askQuit = false;     // la popup doit s'ouvrir
    bool quitting = false;    // sortie confirmee
    void drawQuitPopup();

    glm::vec3 orbitTarget{0.0f};
    float orbitDistance = 10.0f;
    float cameraNear   = 0.1f;
    float cameraFar    = 10000.0f;
    float shadowRadius = 60.0f;
    bool shadowAutoRadius = true;
    bool  focusKeyWas = false;

    void updateEditorCamera();

    void updateBrush();
    bool brushActive = false;
    glm::vec3 brushHitPos{0.0f};
    float brushRadius = 5.0f;
    float brushStrength = 10.0f;
    bool sculptEnabled = false;
    int brushMode = 0;  // 0 = monter/descendre, 1 = lisser, 2 = aplanir
    float flattenTarget = 0.0f; // hauteur de reference capturee au clic (mode aplanir)
    NoiseParams noiseParams;
    float heightmapMin = 0.0f;
    float heightmapMax = 30.0f;

    std::unique_ptr<Window> window;
    std::unique_ptr<VulkanInstance> vulkanInstance;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<VulkanDevice> vulkanDevice;
    VulkanContext vulkanContext;
    std::unique_ptr<Swapchain> swapchain;
    std::unique_ptr<DepthResources> depthResources;
    std::unique_ptr<RenderPass> renderPass;
    std::unique_ptr<UniformBuffer> uniformBuffer;
    std::unique_ptr<CommandPool> commandPool;
    std::unique_ptr<ImGuiLayer> imguiLayer;
    std::unique_ptr<DescriptorSetLayout> descriptorSetLayout;
    std::unique_ptr<DescriptorPool> descriptorPool;
    std::unique_ptr<Pipeline> pipeline;
    std::unique_ptr<Pipeline> gridPipeline;
    std::unique_ptr<Pipeline> transparentPipeline;
    std::unique_ptr<VertexBuffer> gridVertexBuffer;
    uint32_t gridVertexCount = 0;
    std::unique_ptr<Texture> gridDummyTexture;
    std::unique_ptr<Texture> gridDummyNormal;
    std::unique_ptr<DescriptorSet> gridDescriptorSet;
    std::unique_ptr<Framebuffer> framebuffer;
    std::unique_ptr<EnvironmentMap> environment;
    std::unique_ptr<ShadowMap> shadowMap;
    glm::mat4 lightSpaceMatrix{1.0f};
    std::unique_ptr<SkyRenderer> skyRenderer;
    bool showSky = true;
    std::unique_ptr<Pipeline> terrainPipeline;

    VkDescriptorSetLayout materialLayout = VK_NULL_HANDLE;
    std::unique_ptr<MaterialAsset> defaultMaterial;
    std::vector<std::shared_ptr<MaterialAsset>> materials;

    int  selectedMaterial = -1;

    // selectedObjectIndex reste l'objet ACTIF (Inspecteur, gizmo).
    // selectedObjects est l'ensemble de la selection.
    std::vector<int> selectedObjects;

    bool isSelected(int i) const;
    void selectObject(int i, bool additive, bool range);
    void duplicateSelection();
    void deleteSelection();

    int  mapToLoad = 0;
    char materialNameBuf[64] = "Nouveau materiau";
    bool showMaterialPanel = true;

    void drawMaterialPanel();
    void refreshMaterialSets();
    int  createMaterial(const std::string& name);

    std::unique_ptr<Terrain> terrain;
    int terrainResX = 128;
    int terrainResY = 128;
    float terrainSizeX = 100.0f;
    float terrainSizeY = 100.0f;
    float terrainUvTiling = 10.0f;

    float terrainMetallic  = 0.0f;
    float terrainRoughness = 0.85f;

    glm::vec4 terrainLayers = glm::vec4(0.0f, 5.0f, 10.0f, 0.6f);
    bool terrainSelected = false;

    bool gridVisible = true;
    int gridHalfSize = 10;    // etendue en carreaux depuis le centre
    float gridStep = 1.0f;      // espacement des lignes (unites monde)

    std::vector<std::shared_ptr<Mesh>> availableMeshes;
    std::vector<std::string> meshNames;
    size_t builtinMeshCount = 0;
    int selectedMeshIndex = 0;

    bool uiLocked = true;
    ImGuiWindowFlags panelFlags = 0;
    bool showAssetsPanel = true;
    bool showHierarchyPanel = true;
    bool showInspectorPanel = true;
    bool showLightPanel = false;
    bool showSettingsPanel = false;
    int currentTheme = 2;
    std::string sceneFilePath = "scene.json";
    std::string sceneMessage;
    bool requestOpenConfirm = false;
    bool showFpsCounter = true;
    bool showScriptConsole = true;

    glm::vec3 lightDirection{-0.5f, -1.0f, -0.3f};
    glm::vec3 lightColor{1.0f, 1.0f, 1.0f};
    float lightIntensity = 1.0f;
    float ambientStrength = 0.2f;
    float shadowStrength = 0.6f;
    float shininess = 1.0f;
    glm::mat4 projectionMatrix{1.0f};
    glm::mat4 gizmoMatrix{1.0f};   // matrice persistante du gizmo pendant la manipulation
    int gizmoActiveObject = -1;    // objet dont gizmoMatrix reflete l'etat courant

    bool terrainVisible = true;

    int gizmoOperation = 0;  // 0=translate, 1=rotate, 2=scale
    float snapTranslate = 1.0f;   // pas de translation = 1 carreau de grille
    float snapRotate = 45.0f;     // pas de rotation en degres
    float snapScale = 0.25f;      // pas d'echelle

    std::string exportMessage;

    std::vector<std::shared_ptr<SceneObject>> sceneObjects;

    std::unique_ptr<CommandBuffer> commandBuffer;
    std::unique_ptr<SyncObjects> syncObjects;
    std::unique_ptr<Camera> camera;

    std::string currentProjectDir;   // dossier du projet courant (vide = pas encore choisi)
    void createProjectHierarchy(const std::string& dir);
    std::string startupProject;

    std::string pendingProjectParent;
    bool askProjectName = false;
    char projectNameBuf[128] = "MonNiveau";

    struct ObjectSnapshot {
        glm::vec3 position;
        glm::quat orientation;
        glm::vec3 scale;
    };

    bool isPlaying = false;
    std::vector<ObjectSnapshot> playSnapshot;
    ObjectSnapshot terrainSnapshot;
    glm::vec3 playCameraPos{0.0f};
    float playCameraYaw = 0.0f;
    float playCameraPitch = 0.0f;

    void startPlay();
    void stopPlay();
    void updateGameplay(float deltaTime);

    std::unique_ptr<ScriptEngine> scripts;

    std::string scriptStatus;

    bool projectFlowBusy = false;   // un dialogue de projet est en cours

    bool showAxisGizmo = true;

    std::string camDebug;

    struct ObjectState {
        int meshIndex = 0;
        glm::vec3 position{0.0f};
        glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        std::string scriptPath;
        float metallic = 0.0f;
        float roughness = 0.5f;
        std::string name;
        int parent = -1;
        int materialIndex = -1;
    };

    struct Snapshot {
        std::vector<ObjectState> objects;
        int   resX = 128, resY = 128;
        float sizeX = 100.0f, sizeY = 100.0f, uvTiling = 10.0f;
        int   terrainRenderMode = 5;
        glm::vec4 layers{0.0f, 5.0f, 10.0f, 0.6f};
        glm::vec3 tPos{0.0f}, tScale{1.0f};
        glm::quat tOri{1.0f, 0.0f, 0.0f, 0.0f};
        std::shared_ptr<std::vector<float>> heights;
        uint64_t heightsVersion = 0;
        glm::vec3 lightDirection{0.0f}, lightColor{1.0f};
        float lightIntensity = 1.0f, ambientStrength = 0.2f, shininess = 32.0f;
    };

    std::vector<Snapshot> undoStack, redoStack;
    std::shared_ptr<std::vector<float>> heightsCache;
    uint64_t heightsCacheVersion = 0;
    bool gizmoWasUsing = false;

    Snapshot captureState();
    void applySnapshot(const Snapshot& s);
    void pushUndo();
    void undo();
    void redo();

    float exposure = 1.0f;

    enum class EditorMode { Scene, Material, Animation };
    EditorMode editorMode = EditorMode::Scene;
    void setEditorMode(EditorMode m);
    void frameOrbit(const glm::vec3& center, float distance, float yaw, float pitch);

    std::shared_ptr<Mesh> previewSphere;
    uint32_t previewIndexCount = 0;
    glm::vec3 savedCamPos{0.0f};
    float savedCamYaw = 0.0f, savedCamPitch = 0.0f;
    float savedOrbitDistance = 10.0f;
    int animTargetIndex = -1;
    bool showTimelinePanel = true;
    void drawTimelinePanel();

    bool deselectKeyWas = false;
    bool orbitAroundSelection = true;
    InputMap input;
    bool showInputPanel = false;
    int  rebindAction = -1;    // action en cours de reassignation (-1 = aucune)
    bool rebindPad = false;    // true = on attend un bouton de manette
    void drawInputPanel();
    bool frustumCullingEnabled = true;
    int  culledCount = 0;
    int  lastOrbitSelection = -999;

    void updateWorldMatrices();
    void setParent(int child, int newParent);
    void drawObjectNode(int i);

    VkDescriptorSetLayout boneLayout = VK_NULL_HANDLE;
    std::unique_ptr<Pipeline> skinnedPipeline;

    /*
    pool de bonebuffer par un objet skinne
    on ne les detruit jamais pour pas epuiser le descriptor pool
    */
    std::vector<std::unique_ptr<BoneBuffer>> bonePool;
    void updateAnimations(float dt);
    std::vector<AnimationRefData> loadAnimations;
    std::vector<std::shared_ptr<AnimStateMachineAsset>> animMachines;   //assets partages
    void saveAnimMachines();
    void loadAnimMachines();
    std::shared_ptr<AnimStateMachineAsset> findAnimMachine(const std::string& name) const;

    bool showStateMachinePanel = true;
    void drawStateMachinePanel();
    void renameAnimMachine(AnimStateMachineAsset& sm, const std::string& wanted);
    std::string smRenameBuf;    // saisie du nom en cours
    const AnimStateMachineAsset* smRenameTarget = nullptr;
    int smLastObject = -1;      // objet suivi pendant le play

    char animFilter[64] = "";

    int animSelectedNotify = -1;
    bool animDraggingNotify = false; // on déplace la notify pas la tete de lecture
    char animNotifyName[48] = "hit";

};