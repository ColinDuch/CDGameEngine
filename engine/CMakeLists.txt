add_library(engine STATIC
    src/core/Window.cpp
    src/core/Application.cpp
    src/core/Localization.cpp
    src/core/InputMap.cpp
    src/core/ApplicationUI.cpp
    src/core/ApplicationEditor.cpp
    src/core/ApplicationScene.cpp

    src/ui/ImGuiLayer.cpp

    src/script/ScriptEngine.cpp

    src/renderer/VulkanInstance.cpp
    src/renderer/Surface.cpp
    src/renderer/VulkanDevice.cpp
    src/renderer/Swapchain.cpp
    src/renderer/RenderPass.cpp
    src/renderer/Pipeline.cpp
    src/renderer/Framebuffer.cpp
    src/renderer/CommandPool.cpp
    src/renderer/CommandBuffer.cpp
    src/renderer/SyncObjects.cpp
    src/renderer/VertexBuffer.cpp
    src/renderer/UniformBuffer.cpp
    src/renderer/DescriptorSet.cpp
    src/renderer/IndexBuffer.cpp
    src/renderer/BufferUtils.cpp
    src/renderer/Model.cpp
    src/renderer/Texture.cpp
    src/renderer/DepthResources.cpp
    src/renderer/CommandUtils.cpp
    src/renderer/Camera.cpp
    src/renderer/SceneObject.cpp
    src/renderer/MaterialAsset.cpp
    src/renderer/ShadowMap.cpp
    src/renderer/Mesh.cpp
    src/renderer/DescriptorSetLayout.cpp
    src/renderer/DescriptorPool.cpp
    src/renderer/Terrain.cpp
    src/renderer/EnvironmentMap.cpp
    src/renderer/SkyRenderer.cpp
    src/renderer/MeshCache.cpp
    src/renderer/TextureCache.cpp
    src/renderer/BoneBuffer.cpp
    src/renderer/Animation.cpp
    src/renderer/AnimStateMachine.cpp

    src/scene/Base64.cpp
    src/scene/JsonWriter.cpp
    src/scene/GltfExporter.cpp
    src/scene/JsonParser.cpp
    src/scene/SceneSerializer.cpp
    src/scene/AnimStateMachineSerializer.cpp
)

target_include_directories(engine
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(engine
    PUBLIC Vulkan::Vulkan glfw glm::glm imgui
)

target_include_directories(engine PUBLIC ${LUA_INCLUDE_DIR})
target_link_libraries(engine PUBLIC ${LUA_LIBRARIES})

target_compile_features(engine PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(engine PRIVATE
        -Wunused-variable          # une variable declaree et jamais lue
        -Wunused-but-set-variable  # ecrite mais jamais relue -> le cas du 'key'
        -Wunused-function
        -Wreturn-type              # fonction qui oublie de retourner
        -Wparentheses              # priorites douteuses, ternaires suspectes
        -Wmisleading-indentation   # un if sans accolades qui ment sur sa portee
    )
endif()