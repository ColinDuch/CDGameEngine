#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>
#include <vector>

class Pipeline {
public:
    Pipeline(VkDevice device, VkRenderPass renderPass, VkExtent2D swapchainExtent,
            VkDescriptorSetLayout descriptorSetLayout,
            VkDescriptorSetLayout envSetLayout = VK_NULL_HANDLE,
            VkDescriptorSetLayout matSetLayout = VK_NULL_HANDLE,
            VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            const std::string& vertPath = "shaders/vert.spv",
            const std::string& fragPath = "shaders/frag.spv",
            int materialSetCount = 1,
            bool alphaBlend = false,
            VkDescriptorSetLayout boneSetLayout = VK_NULL_HANDLE);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    VkPipeline getHandle() const { return graphicsPipeline; }
    VkPipelineLayout getLayout() const { return pipelineLayout; }

private:
    void createGraphicsPipeline(VkRenderPass renderPass, VkExtent2D swapchainExtent,
                                VkDescriptorSetLayout descriptorSetLayout,
                                VkDescriptorSetLayout envSetLayout,
                                VkDescriptorSetLayout matSetLayout,
                                VkPrimitiveTopology topology,
                                const std::string& vertPath,
                                const std::string& fragPath,
                                int materialSetCount,
                                bool alphaBlend,
                                VkDescriptorSetLayout boneSetLayout);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename);

    VkDevice device;
    VkPipeline graphicsPipeline;
    VkPipelineLayout pipelineLayout;
};