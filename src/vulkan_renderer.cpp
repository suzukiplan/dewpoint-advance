#include "vulkan_renderer.h"

#ifdef LINUX

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <SDL.h>
#include <SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

extern "C" {
extern const unsigned char _binary_crt_vert_spv_start[];
extern const unsigned char _binary_crt_vert_spv_end[];
extern const unsigned char _binary_crt_frag_spv_start[];
extern const unsigned char _binary_crt_frag_spv_end[];
}

namespace
{
constexpr int WINDOW_ASPECT_WIDTH = 3;
constexpr int WINDOW_ASPECT_HEIGHT = 2;

#define VULKAN_INSTANCE_FUNCTIONS(FUNCTION)             \
    FUNCTION(vkDestroyInstance)                         \
    FUNCTION(vkEnumeratePhysicalDevices)                \
    FUNCTION(vkGetPhysicalDeviceProperties)             \
    FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties)  \
    FUNCTION(vkGetPhysicalDeviceSurfaceSupportKHR)      \
    FUNCTION(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    FUNCTION(vkGetPhysicalDeviceSurfaceFormatsKHR)      \
    FUNCTION(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    FUNCTION(vkGetPhysicalDeviceMemoryProperties)       \
    FUNCTION(vkCreateDevice)                            \
    FUNCTION(vkDestroySurfaceKHR)                       \
    FUNCTION(vkGetDeviceProcAddr)

#define VULKAN_DEVICE_FUNCTIONS(FUNCTION)   \
    FUNCTION(vkDestroyDevice)               \
    FUNCTION(vkGetDeviceQueue)              \
    FUNCTION(vkCreateSwapchainKHR)          \
    FUNCTION(vkDestroySwapchainKHR)         \
    FUNCTION(vkGetSwapchainImagesKHR)       \
    FUNCTION(vkCreateImageView)             \
    FUNCTION(vkDestroyImageView)            \
    FUNCTION(vkCreateRenderPass)            \
    FUNCTION(vkDestroyRenderPass)           \
    FUNCTION(vkCreateShaderModule)          \
    FUNCTION(vkDestroyShaderModule)         \
    FUNCTION(vkCreateDescriptorSetLayout)   \
    FUNCTION(vkDestroyDescriptorSetLayout)  \
    FUNCTION(vkCreatePipelineLayout)        \
    FUNCTION(vkDestroyPipelineLayout)       \
    FUNCTION(vkCreateGraphicsPipelines)     \
    FUNCTION(vkDestroyPipeline)             \
    FUNCTION(vkCreateFramebuffer)           \
    FUNCTION(vkDestroyFramebuffer)          \
    FUNCTION(vkCreateCommandPool)           \
    FUNCTION(vkDestroyCommandPool)          \
    FUNCTION(vkAllocateCommandBuffers)      \
    FUNCTION(vkResetCommandBuffer)          \
    FUNCTION(vkBeginCommandBuffer)          \
    FUNCTION(vkEndCommandBuffer)            \
    FUNCTION(vkCreateBuffer)                \
    FUNCTION(vkDestroyBuffer)               \
    FUNCTION(vkGetBufferMemoryRequirements) \
    FUNCTION(vkCreateImage)                 \
    FUNCTION(vkDestroyImage)                \
    FUNCTION(vkGetImageMemoryRequirements)  \
    FUNCTION(vkAllocateMemory)              \
    FUNCTION(vkFreeMemory)                  \
    FUNCTION(vkBindBufferMemory)            \
    FUNCTION(vkBindImageMemory)             \
    FUNCTION(vkMapMemory)                   \
    FUNCTION(vkUnmapMemory)                 \
    FUNCTION(vkCreateSampler)               \
    FUNCTION(vkDestroySampler)              \
    FUNCTION(vkCreateDescriptorPool)        \
    FUNCTION(vkDestroyDescriptorPool)       \
    FUNCTION(vkAllocateDescriptorSets)      \
    FUNCTION(vkUpdateDescriptorSets)        \
    FUNCTION(vkCreateSemaphore)             \
    FUNCTION(vkDestroySemaphore)            \
    FUNCTION(vkCreateFence)                 \
    FUNCTION(vkDestroyFence)                \
    FUNCTION(vkWaitForFences)               \
    FUNCTION(vkResetFences)                 \
    FUNCTION(vkAcquireNextImageKHR)         \
    FUNCTION(vkQueueSubmit)                 \
    FUNCTION(vkQueuePresentKHR)             \
    FUNCTION(vkDeviceWaitIdle)              \
    FUNCTION(vkCmdPipelineBarrier)          \
    FUNCTION(vkCmdCopyBufferToImage)        \
    FUNCTION(vkCmdBeginRenderPass)          \
    FUNCTION(vkCmdEndRenderPass)            \
    FUNCTION(vkCmdSetViewport)              \
    FUNCTION(vkCmdSetScissor)               \
    FUNCTION(vkCmdBindPipeline)             \
    FUNCTION(vkCmdBindDescriptorSets)       \
    FUNCTION(vkCmdPushConstants)            \
    FUNCTION(vkCmdDraw)

struct VulkanApi {
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    PFN_vkCreateInstance vkCreateInstance = nullptr;
#define DECLARE_VULKAN_FUNCTION(name) PFN_##name name = nullptr;
    VULKAN_INSTANCE_FUNCTIONS(DECLARE_VULKAN_FUNCTION)
    VULKAN_DEVICE_FUNCTIONS(DECLARE_VULKAN_FUNCTION)
#undef DECLARE_VULKAN_FUNCTION

    bool loadGlobal()
    {
        vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            SDL_Vulkan_GetVkGetInstanceProcAddr());
        if (!vkGetInstanceProcAddr) {
            std::cerr << "SDL_Vulkan_GetVkGetInstanceProcAddr failed: "
                      << SDL_GetError() << '\n';
            return false;
        }
        vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
            vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
        if (!vkCreateInstance) {
            std::cerr << "Failed to load Vulkan function vkCreateInstance\n";
            return false;
        }
        return true;
    }

    bool loadInstance(VkInstance instance)
    {
#define LOAD_VULKAN_INSTANCE_FUNCTION(name)                                      \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name)); \
    if (!name) {                                                                 \
        std::cerr << "Failed to load Vulkan function " #name "\n";               \
        return false;                                                            \
    }
        VULKAN_INSTANCE_FUNCTIONS(LOAD_VULKAN_INSTANCE_FUNCTION)
#undef LOAD_VULKAN_INSTANCE_FUNCTION
        return true;
    }

    bool loadDevice(VkDevice device)
    {
#define LOAD_VULKAN_DEVICE_FUNCTION(name)                                    \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name)); \
    if (!name) {                                                             \
        std::cerr << "Failed to load Vulkan function " #name "\n";           \
        return false;                                                        \
    }
        VULKAN_DEVICE_FUNCTIONS(LOAD_VULKAN_DEVICE_FUNCTION)
#undef LOAD_VULKAN_DEVICE_FUNCTION
        return true;
    }
};

bool checkResult(VkResult result, const char* operation)
{
    if (result == VK_SUCCESS) {
        return true;
    }
    std::cerr << operation << " failed with Vulkan error " << result << '\n';
    return false;
}

uint32_t clampImageCount(const VkSurfaceCapabilitiesKHR& capabilities)
{
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }
    return imageCount;
}

VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(
    const VkSurfaceCapabilitiesKHR& capabilities)
{
    constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> choices = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (const auto choice : choices) {
        if ((capabilities.supportedCompositeAlpha & choice) != 0) {
            return choice;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}
} // namespace

class VulkanRenderer::Implementation
{
  private:
    struct ShaderConstants {
        float inputSize[2];
        float outputSize[2];
        int32_t filterMode;
    };

    VulkanApi api;
    SDL_Window* window = nullptr;
    bool vulkanLibraryLoaded = false;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = std::numeric_limits<uint32_t>::max();
    VkQueue queue = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    VkImage texture = VK_NULL_HANDLE;
    VkDeviceMemory textureMemory = VK_NULL_HANDLE;
    VkImageView textureView = VK_NULL_HANDLE;
    VkSampler textureSampler = VK_NULL_HANDLE;
    bool textureInitialized = false;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* stagingPixels = nullptr;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderingFinished = VK_NULL_HANDLE;
    VkFence frameFinished = VK_NULL_HANDLE;

    int inputWidth = 0;
    int inputHeight = 0;
    VideoFilter filter = VideoFilter::None;

    bool createInstance()
    {
        unsigned int extensionCount = 0;
        if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr)) {
            std::cerr << "SDL_Vulkan_GetInstanceExtensions failed: "
                      << SDL_GetError() << '\n';
            return false;
        }
        std::vector<const char*> extensions(extensionCount);
        if (!SDL_Vulkan_GetInstanceExtensions(
                window,
                &extensionCount,
                extensions.data())) {
            std::cerr << "SDL_Vulkan_GetInstanceExtensions failed: "
                      << SDL_GetError() << '\n';
            return false;
        }

        const VkApplicationInfo applicationInfo{
            VK_STRUCTURE_TYPE_APPLICATION_INFO,
            nullptr,
            "Dewpoint Advance",
            VK_MAKE_VERSION(1, 0, 0),
            "Dewpoint Vulkan Renderer",
            VK_MAKE_VERSION(1, 0, 0),
            VK_API_VERSION_1_0,
        };
        const VkInstanceCreateInfo createInfo{
            VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            nullptr,
            0,
            &applicationInfo,
            0,
            nullptr,
            extensionCount,
            extensions.data(),
        };
        return checkResult(
                   api.vkCreateInstance(&createInfo, nullptr, &instance),
                   "vkCreateInstance") &&
               api.loadInstance(instance);
    }

    bool physicalDeviceSupportsSwapchain(
        VkPhysicalDevice candidate,
        uint32_t* candidateQueueFamily)
    {
        uint32_t queueFamilyCount = 0;
        api.vkGetPhysicalDeviceQueueFamilyProperties(
            candidate,
            &queueFamilyCount,
            nullptr);
        std::vector<VkQueueFamilyProperties> properties(queueFamilyCount);
        api.vkGetPhysicalDeviceQueueFamilyProperties(
            candidate,
            &queueFamilyCount,
            properties.data());
        for (uint32_t index = 0; index < queueFamilyCount; ++index) {
            VkBool32 presentationSupported = VK_FALSE;
            if ((properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
                api.vkGetPhysicalDeviceSurfaceSupportKHR(
                    candidate,
                    index,
                    surface,
                    &presentationSupported) == VK_SUCCESS &&
                presentationSupported) {
                *candidateQueueFamily = index;
                return true;
            }
        }
        return false;
    }

    bool selectPhysicalDevice()
    {
        uint32_t count = 0;
        if (!checkResult(
                api.vkEnumeratePhysicalDevices(instance, &count, nullptr),
                "vkEnumeratePhysicalDevices") ||
            count == 0) {
            std::cerr << "No Vulkan physical device is available\n";
            return false;
        }
        std::vector<VkPhysicalDevice> devices(count);
        if (!checkResult(
                api.vkEnumeratePhysicalDevices(instance, &count, devices.data()),
                "vkEnumeratePhysicalDevices")) {
            return false;
        }

        int bestScore = -1;
        for (const auto candidate : devices) {
            uint32_t candidateQueueFamily = 0;
            if (!physicalDeviceSupportsSwapchain(
                    candidate,
                    &candidateQueueFamily)) {
                continue;
            }
            VkPhysicalDeviceProperties properties{};
            api.vkGetPhysicalDeviceProperties(candidate, &properties);
            const int score =
                properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1
                                                                                                                                                    : 0;
            if (score > bestScore) {
                physicalDevice = candidate;
                queueFamily = candidateQueueFamily;
                bestScore = score;
            }
        }
        if (physicalDevice == VK_NULL_HANDLE) {
            std::cerr << "No Vulkan device has a graphics/presentation queue\n";
            return false;
        }
        return true;
    }

    bool createDevice()
    {
        constexpr float queuePriority = 1.0F;
        const VkDeviceQueueCreateInfo queueCreateInfo{
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            nullptr,
            0,
            queueFamily,
            1,
            &queuePriority,
        };
        constexpr const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        const VkDeviceCreateInfo createInfo{
            VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            nullptr,
            0,
            1,
            &queueCreateInfo,
            0,
            nullptr,
            1,
            extensions,
            nullptr,
        };
        if (!checkResult(
                api.vkCreateDevice(physicalDevice, &createInfo, nullptr, &device),
                "vkCreateDevice") ||
            !api.loadDevice(device)) {
            return false;
        }
        api.vkGetDeviceQueue(device, queueFamily, 0, &queue);
        return queue != VK_NULL_HANDLE;
    }

    bool findMemoryType(
        uint32_t permittedTypes,
        VkMemoryPropertyFlags requiredProperties,
        uint32_t* memoryType) const
    {
        VkPhysicalDeviceMemoryProperties properties{};
        api.vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
        for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
            if ((permittedTypes & (1U << index)) != 0 &&
                (properties.memoryTypes[index].propertyFlags & requiredProperties) ==
                    requiredProperties) {
                *memoryType = index;
                return true;
            }
        }
        std::cerr << "No compatible Vulkan memory type is available\n";
        return false;
    }

    bool allocateMemory(
        const VkMemoryRequirements& requirements,
        VkMemoryPropertyFlags properties,
        VkDeviceMemory* memory)
    {
        uint32_t memoryType = 0;
        if (!findMemoryType(requirements.memoryTypeBits, properties, &memoryType)) {
            return false;
        }
        const VkMemoryAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            nullptr,
            requirements.size,
            memoryType,
        };
        return checkResult(
            api.vkAllocateMemory(device, &allocateInfo, nullptr, memory),
            "vkAllocateMemory");
    }

    bool createTexture()
    {
        const VkImageCreateInfo imageInfo{
            VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            nullptr,
            0,
            VK_IMAGE_TYPE_2D,
            VK_FORMAT_R8G8B8A8_UNORM,
            {
                static_cast<uint32_t>(inputWidth),
                static_cast<uint32_t>(inputHeight),
                1,
            },
            1,
            1,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            0,
            nullptr,
            VK_IMAGE_LAYOUT_UNDEFINED,
        };
        if (!checkResult(
                api.vkCreateImage(device, &imageInfo, nullptr, &texture),
                "vkCreateImage")) {
            return false;
        }
        VkMemoryRequirements memoryRequirements{};
        api.vkGetImageMemoryRequirements(device, texture, &memoryRequirements);
        if (!allocateMemory(
                memoryRequirements,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &textureMemory) ||
            !checkResult(
                api.vkBindImageMemory(device, texture, textureMemory, 0),
                "vkBindImageMemory")) {
            return false;
        }

        const VkImageViewCreateInfo viewInfo{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            nullptr,
            0,
            texture,
            VK_IMAGE_VIEW_TYPE_2D,
            VK_FORMAT_R8G8B8A8_UNORM,
            {
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                1,
                0,
                1,
            },
        };
        if (!checkResult(
                api.vkCreateImageView(device, &viewInfo, nullptr, &textureView),
                "vkCreateImageView")) {
            return false;
        }

        const VkSamplerCreateInfo samplerInfo{
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            nullptr,
            0,
            VK_FILTER_NEAREST,
            VK_FILTER_NEAREST,
            VK_SAMPLER_MIPMAP_MODE_NEAREST,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            0.0F,
            VK_FALSE,
            1.0F,
            VK_FALSE,
            VK_COMPARE_OP_ALWAYS,
            0.0F,
            0.0F,
            VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            VK_FALSE,
        };
        return checkResult(
            api.vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler),
            "vkCreateSampler");
    }

    bool createStagingBuffer()
    {
        const VkDeviceSize byteSize =
            static_cast<VkDeviceSize>(inputWidth) * inputHeight * 4;
        const VkBufferCreateInfo createInfo{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0,
            byteSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            0,
            nullptr,
        };
        if (!checkResult(
                api.vkCreateBuffer(device, &createInfo, nullptr, &stagingBuffer),
                "vkCreateBuffer")) {
            return false;
        }
        VkMemoryRequirements memoryRequirements{};
        api.vkGetBufferMemoryRequirements(device, stagingBuffer, &memoryRequirements);
        if (!allocateMemory(
                memoryRequirements,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &stagingMemory) ||
            !checkResult(
                api.vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0),
                "vkBindBufferMemory") ||
            !checkResult(
                api.vkMapMemory(
                    device,
                    stagingMemory,
                    0,
                    byteSize,
                    0,
                    &stagingPixels),
                "vkMapMemory")) {
            return false;
        }
        return true;
    }

    bool createDescriptors()
    {
        const VkDescriptorSetLayoutBinding binding{
            0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            1,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            nullptr,
        };
        const VkDescriptorSetLayoutCreateInfo layoutInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            nullptr,
            0,
            1,
            &binding,
        };
        if (!checkResult(
                api.vkCreateDescriptorSetLayout(
                    device,
                    &layoutInfo,
                    nullptr,
                    &descriptorSetLayout),
                "vkCreateDescriptorSetLayout")) {
            return false;
        }

        const VkPushConstantRange pushConstantRange{
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(ShaderConstants),
        };
        const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            nullptr,
            0,
            1,
            &descriptorSetLayout,
            1,
            &pushConstantRange,
        };
        if (!checkResult(
                api.vkCreatePipelineLayout(
                    device,
                    &pipelineLayoutInfo,
                    nullptr,
                    &pipelineLayout),
                "vkCreatePipelineLayout")) {
            return false;
        }

        const VkDescriptorPoolSize poolSize{
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            1,
        };
        const VkDescriptorPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            nullptr,
            0,
            1,
            1,
            &poolSize,
        };
        if (!checkResult(
                api.vkCreateDescriptorPool(
                    device,
                    &poolInfo,
                    nullptr,
                    &descriptorPool),
                "vkCreateDescriptorPool")) {
            return false;
        }

        const VkDescriptorSetAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            nullptr,
            descriptorPool,
            1,
            &descriptorSetLayout,
        };
        if (!checkResult(
                api.vkAllocateDescriptorSets(device, &allocateInfo, &descriptorSet),
                "vkAllocateDescriptorSets")) {
            return false;
        }
        const VkDescriptorImageInfo imageInfo{
            textureSampler,
            textureView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkWriteDescriptorSet write{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            nullptr,
            descriptorSet,
            0,
            0,
            1,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            &imageInfo,
            nullptr,
            nullptr,
        };
        api.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        return true;
    }

    bool createCommandsAndSynchronization()
    {
        const VkCommandPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            nullptr,
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            queueFamily,
        };
        if (!checkResult(
                api.vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool),
                "vkCreateCommandPool")) {
            return false;
        }
        const VkCommandBufferAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            nullptr,
            commandPool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            1,
        };
        if (!checkResult(
                api.vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer),
                "vkAllocateCommandBuffers")) {
            return false;
        }

        const VkSemaphoreCreateInfo semaphoreInfo{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            nullptr,
            0,
        };
        const VkFenceCreateInfo fenceInfo{
            VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            nullptr,
            VK_FENCE_CREATE_SIGNALED_BIT,
        };
        return checkResult(
                   api.vkCreateSemaphore(
                       device,
                       &semaphoreInfo,
                       nullptr,
                       &imageAvailable),
                   "vkCreateSemaphore") &&
               checkResult(
                   api.vkCreateSemaphore(
                       device,
                       &semaphoreInfo,
                       nullptr,
                       &renderingFinished),
                   "vkCreateSemaphore") &&
               checkResult(
                   api.vkCreateFence(device, &fenceInfo, nullptr, &frameFinished),
                   "vkCreateFence");
    }

    bool querySwapchainSupport(
        VkSurfaceCapabilitiesKHR* capabilities,
        std::vector<VkSurfaceFormatKHR>* formats,
        std::vector<VkPresentModeKHR>* presentModes)
    {
        if (!checkResult(
                api.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                    physicalDevice,
                    surface,
                    capabilities),
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) {
            return false;
        }
        uint32_t formatCount = 0;
        if (!checkResult(
                api.vkGetPhysicalDeviceSurfaceFormatsKHR(
                    physicalDevice,
                    surface,
                    &formatCount,
                    nullptr),
                "vkGetPhysicalDeviceSurfaceFormatsKHR") ||
            formatCount == 0) {
            return false;
        }
        formats->resize(formatCount);
        if (!checkResult(
                api.vkGetPhysicalDeviceSurfaceFormatsKHR(
                    physicalDevice,
                    surface,
                    &formatCount,
                    formats->data()),
                "vkGetPhysicalDeviceSurfaceFormatsKHR")) {
            return false;
        }
        uint32_t presentModeCount = 0;
        if (!checkResult(
                api.vkGetPhysicalDeviceSurfacePresentModesKHR(
                    physicalDevice,
                    surface,
                    &presentModeCount,
                    nullptr),
                "vkGetPhysicalDeviceSurfacePresentModesKHR") ||
            presentModeCount == 0) {
            return false;
        }
        presentModes->resize(presentModeCount);
        return checkResult(
            api.vkGetPhysicalDeviceSurfacePresentModesKHR(
                physicalDevice,
                surface,
                &presentModeCount,
                presentModes->data()),
            "vkGetPhysicalDeviceSurfacePresentModesKHR");
    }

    VkExtent2D chooseSwapchainExtent(
        const VkSurfaceCapabilitiesKHR& capabilities) const
    {
        if (capabilities.currentExtent.width !=
            std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        int width = 0;
        int height = 0;
        SDL_Vulkan_GetDrawableSize(window, &width, &height);
        return {
            std::clamp(
                static_cast<uint32_t>(std::max(width, 0)),
                capabilities.minImageExtent.width,
                capabilities.maxImageExtent.width),
            std::clamp(
                static_cast<uint32_t>(std::max(height, 0)),
                capabilities.minImageExtent.height,
                capabilities.maxImageExtent.height),
        };
    }

    bool createRenderPass()
    {
        const VkAttachmentDescription colorAttachment{
            0,
            swapchainFormat,
            VK_SAMPLE_COUNT_1_BIT,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };
        const VkAttachmentReference colorReference{
            0,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        const VkSubpassDescription subpass{
            0,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            0,
            nullptr,
            1,
            &colorReference,
            nullptr,
            nullptr,
            0,
            nullptr,
        };
        const VkSubpassDependency dependency{
            VK_SUBPASS_EXTERNAL,
            0,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            0,
        };
        const VkRenderPassCreateInfo createInfo{
            VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            nullptr,
            0,
            1,
            &colorAttachment,
            1,
            &subpass,
            1,
            &dependency,
        };
        return checkResult(
            api.vkCreateRenderPass(device, &createInfo, nullptr, &renderPass),
            "vkCreateRenderPass");
    }

    bool createShaderModule(
        const unsigned char* begin,
        const unsigned char* end,
        VkShaderModule* shaderModule)
    {
        const size_t byteSize = static_cast<size_t>(end - begin);
        if ((byteSize % sizeof(uint32_t)) != 0) {
            std::cerr << "Invalid embedded SPIR-V shader size\n";
            return false;
        }
        const VkShaderModuleCreateInfo createInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            nullptr,
            0,
            byteSize,
            reinterpret_cast<const uint32_t*>(begin),
        };
        return checkResult(
            api.vkCreateShaderModule(device, &createInfo, nullptr, shaderModule),
            "vkCreateShaderModule");
    }

    bool createPipeline()
    {
        VkShaderModule vertexShader = VK_NULL_HANDLE;
        VkShaderModule fragmentShader = VK_NULL_HANDLE;
        if (!createShaderModule(
                _binary_crt_vert_spv_start,
                _binary_crt_vert_spv_end,
                &vertexShader) ||
            !createShaderModule(
                _binary_crt_frag_spv_start,
                _binary_crt_frag_spv_end,
                &fragmentShader)) {
            if (vertexShader != VK_NULL_HANDLE) {
                api.vkDestroyShaderModule(device, vertexShader, nullptr);
            }
            return false;
        }

        const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {{
            {
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                nullptr,
                0,
                VK_SHADER_STAGE_VERTEX_BIT,
                vertexShader,
                "main",
                nullptr,
            },
            {
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                nullptr,
                0,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                fragmentShader,
                "main",
                nullptr,
            },
        }};
        const VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            nullptr,
            0,
            0,
            nullptr,
            0,
            nullptr,
        };
        const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            nullptr,
            0,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            VK_FALSE,
        };
        const VkPipelineViewportStateCreateInfo viewportState{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            nullptr,
            0,
            1,
            nullptr,
            1,
            nullptr,
        };
        const VkPipelineRasterizationStateCreateInfo rasterization{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            nullptr,
            0,
            VK_FALSE,
            VK_FALSE,
            VK_POLYGON_MODE_FILL,
            VK_CULL_MODE_NONE,
            VK_FRONT_FACE_COUNTER_CLOCKWISE,
            VK_FALSE,
            0.0F,
            0.0F,
            0.0F,
            1.0F,
        };
        const VkPipelineMultisampleStateCreateInfo multisampling{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            nullptr,
            0,
            VK_SAMPLE_COUNT_1_BIT,
            VK_FALSE,
            1.0F,
            nullptr,
            VK_FALSE,
            VK_FALSE,
        };
        const VkPipelineColorBlendAttachmentState colorBlendAttachment{
            VK_FALSE,
            VK_BLEND_FACTOR_ONE,
            VK_BLEND_FACTOR_ZERO,
            VK_BLEND_OP_ADD,
            VK_BLEND_FACTOR_ONE,
            VK_BLEND_FACTOR_ZERO,
            VK_BLEND_OP_ADD,
            VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT,
        };
        const VkPipelineColorBlendStateCreateInfo colorBlending{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            nullptr,
            0,
            VK_FALSE,
            VK_LOGIC_OP_COPY,
            1,
            &colorBlendAttachment,
            {0.0F, 0.0F, 0.0F, 0.0F},
        };
        constexpr std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        const VkPipelineDynamicStateCreateInfo dynamicState{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            nullptr,
            0,
            static_cast<uint32_t>(dynamicStates.size()),
            dynamicStates.data(),
        };
        const VkGraphicsPipelineCreateInfo createInfo{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            nullptr,
            0,
            static_cast<uint32_t>(stages.size()),
            stages.data(),
            &vertexInput,
            &inputAssembly,
            nullptr,
            &viewportState,
            &rasterization,
            &multisampling,
            nullptr,
            &colorBlending,
            &dynamicState,
            pipelineLayout,
            renderPass,
            0,
            VK_NULL_HANDLE,
            -1,
        };
        const bool created = checkResult(
            api.vkCreateGraphicsPipelines(
                device,
                VK_NULL_HANDLE,
                1,
                &createInfo,
                nullptr,
                &pipeline),
            "vkCreateGraphicsPipelines");
        api.vkDestroyShaderModule(device, fragmentShader, nullptr);
        api.vkDestroyShaderModule(device, vertexShader, nullptr);
        return created;
    }

    bool createSwapchain()
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
        if (!querySwapchainSupport(&capabilities, &formats, &presentModes)) {
            return false;
        }
        swapchainExtent = chooseSwapchainExtent(capabilities);
        if (swapchainExtent.width == 0 || swapchainExtent.height == 0) {
            return true;
        }

        VkSurfaceFormatKHR selectedFormat = formats.front();
        if (formats.size() == 1 &&
            selectedFormat.format == VK_FORMAT_UNDEFINED) {
            selectedFormat.format = VK_FORMAT_B8G8R8A8_UNORM;
        }
        for (const auto& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                selectedFormat = format;
                break;
            }
        }
        swapchainFormat = selectedFormat.format;
        const VkSwapchainCreateInfoKHR createInfo{
            VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            nullptr,
            0,
            surface,
            clampImageCount(capabilities),
            selectedFormat.format,
            selectedFormat.colorSpace,
            swapchainExtent,
            1,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            0,
            nullptr,
            capabilities.currentTransform,
            chooseCompositeAlpha(capabilities),
            VK_PRESENT_MODE_FIFO_KHR,
            VK_TRUE,
            VK_NULL_HANDLE,
        };
        if (!checkResult(
                api.vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain),
                "vkCreateSwapchainKHR")) {
            return false;
        }

        uint32_t imageCount = 0;
        if (!checkResult(
                api.vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr),
                "vkGetSwapchainImagesKHR") ||
            imageCount == 0) {
            return false;
        }
        swapchainImages.resize(imageCount);
        if (!checkResult(
                api.vkGetSwapchainImagesKHR(
                    device,
                    swapchain,
                    &imageCount,
                    swapchainImages.data()),
                "vkGetSwapchainImagesKHR")) {
            return false;
        }

        swapchainImageViews.resize(imageCount, VK_NULL_HANDLE);
        for (size_t index = 0; index < swapchainImages.size(); ++index) {
            const VkImageViewCreateInfo viewInfo{
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                nullptr,
                0,
                swapchainImages[index],
                VK_IMAGE_VIEW_TYPE_2D,
                swapchainFormat,
                {
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                },
                {
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    0,
                    1,
                    0,
                    1,
                },
            };
            if (!checkResult(
                    api.vkCreateImageView(
                        device,
                        &viewInfo,
                        nullptr,
                        &swapchainImageViews[index]),
                    "vkCreateImageView")) {
                return false;
            }
        }
        if (!createRenderPass() || !createPipeline()) {
            return false;
        }

        framebuffers.resize(imageCount, VK_NULL_HANDLE);
        for (size_t index = 0; index < swapchainImageViews.size(); ++index) {
            const VkFramebufferCreateInfo framebufferInfo{
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                nullptr,
                0,
                renderPass,
                1,
                &swapchainImageViews[index],
                swapchainExtent.width,
                swapchainExtent.height,
                1,
            };
            if (!checkResult(
                    api.vkCreateFramebuffer(
                        device,
                        &framebufferInfo,
                        nullptr,
                        &framebuffers[index]),
                    "vkCreateFramebuffer")) {
                return false;
            }
        }
        return true;
    }

    void destroySwapchain()
    {
        for (const auto framebuffer : framebuffers) {
            if (framebuffer != VK_NULL_HANDLE) {
                api.vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
        }
        framebuffers.clear();
        if (pipeline != VK_NULL_HANDLE) {
            api.vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        if (renderPass != VK_NULL_HANDLE) {
            api.vkDestroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }
        for (const auto imageView : swapchainImageViews) {
            if (imageView != VK_NULL_HANDLE) {
                api.vkDestroyImageView(device, imageView, nullptr);
            }
        }
        swapchainImageViews.clear();
        swapchainImages.clear();
        if (swapchain != VK_NULL_HANDLE) {
            api.vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
        swapchainExtent = {};
        swapchainFormat = VK_FORMAT_UNDEFINED;
    }

    bool recreateSwapchain()
    {
        if (!checkResult(api.vkDeviceWaitIdle(device), "vkDeviceWaitIdle")) {
            return false;
        }
        destroySwapchain();
        return createSwapchain();
    }

    void recordTextureUpload()
    {
        const VkImageMemoryBarrier beforeCopy{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            nullptr,
            textureInitialized ? static_cast<VkAccessFlags>(VK_ACCESS_SHADER_READ_BIT) : VkAccessFlags{0},
            VK_ACCESS_TRANSFER_WRITE_BIT,
            textureInitialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            texture,
            {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                1,
                0,
                1,
            },
        };
        api.vkCmdPipelineBarrier(
            commandBuffer,
            textureInitialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &beforeCopy);

        const VkBufferImageCopy copy{
            0,
            0,
            0,
            {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                0,
                1,
            },
            {0, 0, 0},
            {
                static_cast<uint32_t>(inputWidth),
                static_cast<uint32_t>(inputHeight),
                1,
            },
        };
        api.vkCmdCopyBufferToImage(
            commandBuffer,
            stagingBuffer,
            texture,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copy);

        const VkImageMemoryBarrier afterCopy{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            texture,
            {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                1,
                0,
                1,
            },
        };
        api.vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &afterCopy);
        textureInitialized = true;
    }

    void recordDraw(uint32_t imageIndex)
    {
        const VkClearValue clearColor{{{0.0F, 0.0F, 0.0F, 1.0F}}};
        const VkRenderPassBeginInfo renderPassInfo{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            nullptr,
            renderPass,
            framebuffers[imageIndex],
            {{0, 0}, swapchainExtent},
            1,
            &clearColor,
        };
        api.vkCmdBeginRenderPass(
            commandBuffer,
            &renderPassInfo,
            VK_SUBPASS_CONTENTS_INLINE);

        uint32_t viewportWidth = swapchainExtent.width;
        uint32_t viewportHeight =
            viewportWidth * WINDOW_ASPECT_HEIGHT / WINDOW_ASPECT_WIDTH;
        if (viewportHeight > swapchainExtent.height) {
            viewportHeight = swapchainExtent.height;
            viewportWidth =
                viewportHeight * WINDOW_ASPECT_WIDTH / WINDOW_ASPECT_HEIGHT;
        }
        const uint32_t viewportX = (swapchainExtent.width - viewportWidth) / 2;
        const uint32_t viewportY = (swapchainExtent.height - viewportHeight) / 2;
        const VkViewport viewport{
            static_cast<float>(viewportX),
            static_cast<float>(viewportY),
            static_cast<float>(viewportWidth),
            static_cast<float>(viewportHeight),
            0.0F,
            1.0F,
        };
        const VkRect2D scissor{
            {
                static_cast<int32_t>(viewportX),
                static_cast<int32_t>(viewportY),
            },
            {viewportWidth, viewportHeight},
        };
        const ShaderConstants constants{
            {
                static_cast<float>(inputWidth),
                static_cast<float>(inputHeight),
            },
            {
                static_cast<float>(viewportWidth),
                static_cast<float>(viewportHeight),
            },
            static_cast<int32_t>(filter),
        };
        api.vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        api.vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        api.vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline);
        api.vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            0,
            1,
            &descriptorSet,
            0,
            nullptr);
        api.vkCmdPushConstants(
            commandBuffer,
            pipelineLayout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(constants),
            &constants);
        api.vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        api.vkCmdEndRenderPass(commandBuffer);
    }

  public:
    ~Implementation()
    {
        shutdown();
    }

    bool initialize(
        SDL_Window* targetWindow,
        int width,
        int height,
        VideoFilter selectedFilter)
    {
        window = targetWindow;
        inputWidth = width;
        inputHeight = height;
        filter = selectedFilter;

        if (SDL_Vulkan_LoadLibrary(nullptr) != 0) {
            std::cerr << "SDL_Vulkan_LoadLibrary failed: " << SDL_GetError() << '\n';
            return false;
        }
        vulkanLibraryLoaded = true;
        if (!api.loadGlobal() || !createInstance()) {
            return false;
        }
        if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
            std::cerr << "SDL_Vulkan_CreateSurface failed: "
                      << SDL_GetError() << '\n';
            return false;
        }
        if (!selectPhysicalDevice() ||
            !createDevice() ||
            !createTexture() ||
            !createStagingBuffer() ||
            !createDescriptors() ||
            !createCommandsAndSynchronization() ||
            !createSwapchain()) {
            return false;
        }
        return true;
    }

    bool usesVsync() const
    {
        return true;
    }

    void present(const void* pixels)
    {
        if (device == VK_NULL_HANDLE || !pixels) {
            return;
        }
        if (swapchain == VK_NULL_HANDLE) {
            if (!recreateSwapchain() || swapchain == VK_NULL_HANDLE) {
                return;
            }
        }
        if (!checkResult(
                api.vkWaitForFences(
                    device,
                    1,
                    &frameFinished,
                    VK_TRUE,
                    std::numeric_limits<uint64_t>::max()),
                "vkWaitForFences")) {
            return;
        }

        uint32_t imageIndex = 0;
        const VkResult acquireResult = api.vkAcquireNextImageKHR(
            device,
            swapchain,
            std::numeric_limits<uint64_t>::max(),
            imageAvailable,
            VK_NULL_HANDLE,
            &imageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return;
        }
        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            checkResult(acquireResult, "vkAcquireNextImageKHR");
            return;
        }

        std::memcpy(
            stagingPixels,
            pixels,
            static_cast<size_t>(inputWidth) * inputHeight * 4);
        if (!checkResult(
                api.vkResetCommandBuffer(commandBuffer, 0),
                "vkResetCommandBuffer")) {
            return;
        }
        const VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            nullptr,
        };
        if (!checkResult(
                api.vkBeginCommandBuffer(commandBuffer, &beginInfo),
                "vkBeginCommandBuffer")) {
            return;
        }
        recordTextureUpload();
        recordDraw(imageIndex);
        if (!checkResult(
                api.vkEndCommandBuffer(commandBuffer),
                "vkEndCommandBuffer") ||
            !checkResult(
                api.vkResetFences(device, 1, &frameFinished),
                "vkResetFences")) {
            return;
        }

        constexpr VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkSubmitInfo submitInfo{
            VK_STRUCTURE_TYPE_SUBMIT_INFO,
            nullptr,
            1,
            &imageAvailable,
            &waitStage,
            1,
            &commandBuffer,
            1,
            &renderingFinished,
        };
        if (!checkResult(
                api.vkQueueSubmit(queue, 1, &submitInfo, frameFinished),
                "vkQueueSubmit")) {
            return;
        }
        const VkPresentInfoKHR presentInfo{
            VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            nullptr,
            1,
            &renderingFinished,
            1,
            &swapchain,
            &imageIndex,
            nullptr,
        };
        const VkResult presentResult = api.vkQueuePresentKHR(queue, &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
            presentResult == VK_SUBOPTIMAL_KHR ||
            acquireResult == VK_SUBOPTIMAL_KHR) {
            recreateSwapchain();
        } else {
            checkResult(presentResult, "vkQueuePresentKHR");
        }
    }

    void shutdown()
    {
        if (device != VK_NULL_HANDLE && api.vkDeviceWaitIdle) {
            api.vkDeviceWaitIdle(device);
            destroySwapchain();
            if (frameFinished != VK_NULL_HANDLE) {
                api.vkDestroyFence(device, frameFinished, nullptr);
                frameFinished = VK_NULL_HANDLE;
            }
            if (renderingFinished != VK_NULL_HANDLE) {
                api.vkDestroySemaphore(device, renderingFinished, nullptr);
                renderingFinished = VK_NULL_HANDLE;
            }
            if (imageAvailable != VK_NULL_HANDLE) {
                api.vkDestroySemaphore(device, imageAvailable, nullptr);
                imageAvailable = VK_NULL_HANDLE;
            }
            if (commandPool != VK_NULL_HANDLE) {
                api.vkDestroyCommandPool(device, commandPool, nullptr);
                commandPool = VK_NULL_HANDLE;
                commandBuffer = VK_NULL_HANDLE;
            }
            if (descriptorPool != VK_NULL_HANDLE) {
                api.vkDestroyDescriptorPool(device, descriptorPool, nullptr);
                descriptorPool = VK_NULL_HANDLE;
                descriptorSet = VK_NULL_HANDLE;
            }
            if (pipelineLayout != VK_NULL_HANDLE) {
                api.vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
                pipelineLayout = VK_NULL_HANDLE;
            }
            if (descriptorSetLayout != VK_NULL_HANDLE) {
                api.vkDestroyDescriptorSetLayout(
                    device,
                    descriptorSetLayout,
                    nullptr);
                descriptorSetLayout = VK_NULL_HANDLE;
            }
            if (textureSampler != VK_NULL_HANDLE) {
                api.vkDestroySampler(device, textureSampler, nullptr);
                textureSampler = VK_NULL_HANDLE;
            }
            if (textureView != VK_NULL_HANDLE) {
                api.vkDestroyImageView(device, textureView, nullptr);
                textureView = VK_NULL_HANDLE;
            }
            if (texture != VK_NULL_HANDLE) {
                api.vkDestroyImage(device, texture, nullptr);
                texture = VK_NULL_HANDLE;
            }
            if (textureMemory != VK_NULL_HANDLE) {
                api.vkFreeMemory(device, textureMemory, nullptr);
                textureMemory = VK_NULL_HANDLE;
            }
            if (stagingPixels) {
                api.vkUnmapMemory(device, stagingMemory);
                stagingPixels = nullptr;
            }
            if (stagingBuffer != VK_NULL_HANDLE) {
                api.vkDestroyBuffer(device, stagingBuffer, nullptr);
                stagingBuffer = VK_NULL_HANDLE;
            }
            if (stagingMemory != VK_NULL_HANDLE) {
                api.vkFreeMemory(device, stagingMemory, nullptr);
                stagingMemory = VK_NULL_HANDLE;
            }
            api.vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
        if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE &&
            api.vkDestroySurfaceKHR) {
            api.vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
        if (instance != VK_NULL_HANDLE && api.vkDestroyInstance) {
            api.vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }
        if (vulkanLibraryLoaded) {
            SDL_Vulkan_UnloadLibrary();
            vulkanLibraryLoaded = false;
        }
        window = nullptr;
    }
};

VulkanRenderer::VulkanRenderer()
    : implementation(std::make_unique<Implementation>())
{
}

VulkanRenderer::~VulkanRenderer() = default;

bool VulkanRenderer::initialize(
    SDL_Window* window,
    int width,
    int height,
    VideoFilter filter)
{
    return implementation->initialize(window, width, height, filter);
}

bool VulkanRenderer::usesVsync() const
{
    return implementation->usesVsync();
}

void VulkanRenderer::present(const void* pixels)
{
    implementation->present(pixels);
}

void VulkanRenderer::shutdown()
{
    implementation->shutdown();
}

#else

class VulkanRenderer::Implementation
{
};

VulkanRenderer::VulkanRenderer()
    : implementation(std::make_unique<Implementation>())
{
}

VulkanRenderer::~VulkanRenderer() = default;

bool VulkanRenderer::initialize(SDL_Window*, int, int, VideoFilter)
{
    return false;
}

bool VulkanRenderer::usesVsync() const
{
    return false;
}

void VulkanRenderer::present(const void*)
{
}

void VulkanRenderer::shutdown()
{
}

#endif
