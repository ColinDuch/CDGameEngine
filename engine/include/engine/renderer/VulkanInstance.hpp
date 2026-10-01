#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>

class VulkanInstance {
public:
    VulkanInstance(const std::vector<const char*>& requiredExtensions);
    ~VulkanInstance();

    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    VkInstance getHandle() const { return instance; }

private:
    void createInstance(const std::vector<const char*>& requiredExtensions);
    bool checkValidationLayerSupport();
    void setupDebugMessenger();
    std::vector<const char*> getExtensionsWithDebug(const std::vector<const char*>& requiredExtensions);

    static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback( VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;

#ifdef NDEBUG
    bool enableValidationLayers = false;
#else
    bool enableValidationLayers = true;
#endif

    const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };
};