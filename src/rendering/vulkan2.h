#pragma once

#include "openxr.h"
#include "texture.h"
#include "utils/render_utils.h"

class RND_Renderer;

// pch.h defines VK_NO_PROTOTYPES (needed so this layer's own exported hook functions,
// e.g. vkCreateInstance, don't collide with real declarations of the same name). That
// means Device B - a genuinely separate, unhooked device - can't just call "vkCreateImage(...)"
// etc. directly; every function it needs has to be loaded explicitly as a function
// pointer instead, the same way a Vulkan loader bootstraps itself. This struct holds
// all of them, loaded once in RND_Vulkan2's constructor.
struct Vulkan2Functions {
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
    PFN_vkDestroyImage vkDestroyImage = nullptr;
    PFN_vkCreateImage vkCreateImage = nullptr;
    PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements = nullptr;
    PFN_vkAllocateMemory vkAllocateMemory = nullptr;
    PFN_vkFreeMemory vkFreeMemory = nullptr;
    PFN_vkBindImageMemory vkBindImageMemory = nullptr;
    PFN_vkCreateImageView vkCreateImageView = nullptr;
    PFN_vkDestroyImageView vkDestroyImageView = nullptr;
    PFN_vkCreateSemaphore vkCreateSemaphore = nullptr;
    PFN_vkDestroySemaphore vkDestroySemaphore = nullptr;
    PFN_vkImportSemaphoreFdKHR vkImportSemaphoreFdKHR = nullptr;
    PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHR = nullptr;
    PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR = nullptr;
    PFN_vkQueueSubmit vkQueueSubmit = nullptr;
    PFN_vkQueueWaitIdle vkQueueWaitIdle = nullptr;
    PFN_vkWaitSemaphores vkWaitSemaphores = nullptr;
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = nullptr;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
    PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
    PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
    PFN_vkFreeCommandBuffers vkFreeCommandBuffers = nullptr;
    PFN_vkResetCommandPool vkResetCommandPool = nullptr;
    PFN_vkCreateBuffer vkCreateBuffer = nullptr;
    PFN_vkDestroyBuffer vkDestroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
    PFN_vkBindBufferMemory vkBindBufferMemory = nullptr;
    PFN_vkMapMemory vkMapMemory = nullptr;
    PFN_vkUnmapMemory vkUnmapMemory = nullptr;
    PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout = nullptr;
    PFN_vkCreatePipelineLayout vkCreatePipelineLayout = nullptr;
    PFN_vkCreateSampler vkCreateSampler = nullptr;
    PFN_vkCreateDescriptorPool vkCreateDescriptorPool = nullptr;
    PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets = nullptr;
    PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets = nullptr;
    PFN_vkCreateShaderModule vkCreateShaderModule = nullptr;
    PFN_vkDestroyShaderModule vkDestroyShaderModule = nullptr;
    PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines = nullptr;
    PFN_vkDestroyPipeline vkDestroyPipeline = nullptr;
    PFN_vkCmdBeginRendering vkCmdBeginRendering = nullptr;
    PFN_vkCmdEndRendering vkCmdEndRendering = nullptr;
    PFN_vkCmdBindPipeline vkCmdBindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets = nullptr;
    PFN_vkCmdSetViewport vkCmdSetViewport = nullptr;
    PFN_vkCmdSetScissor vkCmdSetScissor = nullptr;
    PFN_vkCmdDraw vkCmdDraw = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2 = nullptr;
    PFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;

    // Loads every member above via instanceLoad(instance, "vkX") for instance-level
    // functions and vkGetDeviceProcAddr(device, "vkX") (once that's itself loaded)
    // for device-level ones. Defined in vulkan2.cpp.
    void Load(PFN_vkGetInstanceProcAddr instanceLoad, VkInstance instance, VkDevice device);
};

// "Device B": a second, separate, UNHOOKED Vulkan device (own VkInstance, own
// VkDevice) created through XR_KHR_vulkan_enable2, used only to talk to OpenXR/WiVRn.
// This replaces RND_D3D12 - same role (composite Device A's shared textures into the
// real OpenXR swapchain images and submit the frame), same "separate device so we
// don't recurse into our own Vulkan hook" reasoning that originally justified D3D12
// on Windows, just Vulkan-to-Vulkan instead of Vulkan-to-D3D12. This exact
// architecture (two Vulkan devices + FD-based external memory/semaphore sharing +
// XR_KHR_vulkan_enable2) was validated standalone against a real Meta Quest 3
// through WiVRn before being wired in here - see vk-vk-openxr-test/.
class RND_Vulkan2 {
    friend class RND_Renderer;

public:
    // Takes the main OpenXR instance/system directly rather than reaching through
    // VRManager::instance() - this is constructed from inside VRManager's own
    // constructor (before its Meyer's-singleton static-init has finished), so
    // calling back into VRManager::instance() here would recursively re-enter an
    // in-progress function-local static initialization (undefined behavior).
    RND_Vulkan2(XrInstance xrInstance, XrSystemId systemId);
    ~RND_Vulkan2();

    VkInstance GetInstance() { return m_instance; }
    VkPhysicalDevice GetPhysicalDevice() { return m_physicalDevice; }
    VkDevice GetDevice() { return m_device; }
    VkQueue GetQueue() { return m_queue; }
    uint32_t GetQueueFamilyIndex() { return m_queueFamilyIndex; }
    const Vulkan2Functions& fn() const { return m_functions; }
    std::array<uint8_t, VK_UUID_SIZE> GetPhysicalDeviceUUID() const;

    uint32_t FindMemoryType(uint32_t memoryTypeBitsRequirement, VkMemoryPropertyFlags requirementsMask);

    void StartFrame();
    void EndFrame();

    // todo: extract most to a base pipeline class if other pipelines are needed
    class PresentPipeline {
        friend class Texture;

    public:
        explicit PresentPipeline(RND_Renderer* pRenderer);
        ~PresentPipeline();

        void BindAttachment(uint32_t attachmentIdx, VkImageView srcView);
        void BindFadeSample(VkImageView fadeView);
        void BindSettings(float screenWidth, float screenHeight, const RenderUtils::UvTransform& uvTransform = {});
        void SetUvTransform(const RenderUtils::UvTransform& uvTransform) { m_uvTransform = uvTransform; }
        // dstImage/dstView must already be in VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        void Render(VkCommandBuffer cmdBuffer, VkImage dstImage, VkImageView dstView, VkFormat dstFormat, uint32_t width, uint32_t height);

    private:
        void UpdateSettingsBuffer(uint32_t swapchainWidth, uint32_t swapchainHeight);
        void EnsurePipeline(VkFormat colorFormat);
        void UpdateDescriptorSet();

        RND_Renderer* m_renderer = nullptr;
        float m_renderWidth = 0.0f;
        float m_renderHeight = 0.0f;
        RenderUtils::UvTransform m_uvTransform = {};

        VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_pipeline = VK_NULL_HANDLE;
        VkFormat m_pipelineColorFormat = VK_FORMAT_UNDEFINED;

        VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
        VkSampler m_sampler = VK_NULL_HANDLE;

        VkImageView m_attachmentView = VK_NULL_HANDLE;
        VkImageView m_fadeSampleView = VK_NULL_HANDLE;
        bool m_descriptorSetDirty = true;

        VkBuffer m_settingsBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_settingsMemory = VK_NULL_HANDLE;
        void* m_settingsMapped = nullptr;
    };

    template <bool blockTillExecuted>
    class CommandContext {
    public:
        template <typename F>
        CommandContext(RND_Vulkan2* vk2, F&& recordCallback): m_vk2(vk2) {
            checkAssert(m_vk2 != nullptr, "Vulkan2 command context is missing its renderer backend!");
            m_waitFor.reserve(4);
            m_signalTo.reserve(4);

            if constexpr (blockTillExecuted) {
                m_cmdBuffer = m_vk2->AcquireImmediateCommandBuffer();
            }
            else {
                m_cmdBuffer = m_vk2->AcquireFrameCommandBuffer();
            }

            VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            checkVkResult(m_vk2->fn().vkBeginCommandBuffer(m_cmdBuffer, &beginInfo), "Failed to begin Vulkan2 command buffer!");

            recordCallback(this);
        }

        ~CommandContext() {
            checkVkResult(m_vk2->fn().vkEndCommandBuffer(m_cmdBuffer), "Failed to end Vulkan2 command buffer!");

            std::vector<VkSemaphore> waitSemaphores;
            std::vector<uint64_t> waitValues;
            std::vector<VkPipelineStageFlags> waitStages;
            for (auto& [texture, value] : m_waitFor) {
                waitSemaphores.push_back(texture->secondDeviceGetSemaphore());
                waitValues.push_back(value);
                waitStages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            }
            std::vector<VkSemaphore> signalSemaphores;
            std::vector<uint64_t> signalValues;
            for (auto& [texture, value] : m_signalTo) {
                signalSemaphores.push_back(texture->secondDeviceGetSemaphore());
                signalValues.push_back(value);
            }

            VkTimelineSemaphoreSubmitInfo timelineInfo = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
            timelineInfo.waitSemaphoreValueCount = (uint32_t)waitValues.size();
            timelineInfo.pWaitSemaphoreValues = waitValues.data();
            timelineInfo.signalSemaphoreValueCount = (uint32_t)signalValues.size();
            timelineInfo.pSignalSemaphoreValues = signalValues.data();

            VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submit.pNext = &timelineInfo;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &m_cmdBuffer;
            submit.waitSemaphoreCount = (uint32_t)waitSemaphores.size();
            submit.pWaitSemaphores = waitSemaphores.empty() ? nullptr : waitSemaphores.data();
            submit.pWaitDstStageMask = waitStages.empty() ? nullptr : waitStages.data();
            submit.signalSemaphoreCount = (uint32_t)signalSemaphores.size();
            submit.pSignalSemaphores = signalSemaphores.empty() ? nullptr : signalSemaphores.data();

            checkVkResult(m_vk2->fn().vkQueueSubmit(m_vk2->GetQueue(), 1, &submit, VK_NULL_HANDLE), "Failed to submit Vulkan2 command buffer!");

            if constexpr (blockTillExecuted) {
                checkVkResult(m_vk2->fn().vkQueueWaitIdle(m_vk2->GetQueue()), "Failed to wait for Vulkan2 queue idle!");
            }
        }

        VkCommandBuffer GetRecordList() { return m_cmdBuffer; }
        void WaitFor(SecondDeviceTexture* texture, uint64_t value) { m_waitFor.push_back({ texture, value }); }
        void Signal(SecondDeviceTexture* texture, uint64_t value) { m_signalTo.push_back({ texture, value }); }

    private:
        RND_Vulkan2* m_vk2 = nullptr;
        VkCommandBuffer m_cmdBuffer = VK_NULL_HANDLE;
        std::vector<std::pair<SecondDeviceTexture*, uint64_t>> m_waitFor;
        std::vector<std::pair<SecondDeviceTexture*, uint64_t>> m_signalTo;
    };

private:
    static constexpr uint32_t kFrameContextCount = 3;
    static constexpr uint32_t kFrameCommandBufferCount = 4;

    struct FrameContext {
        VkCommandPool pool = VK_NULL_HANDLE;
        std::array<VkCommandBuffer, kFrameCommandBufferCount> commandBuffers = {};
        uint32_t nextCommandBufferIndex = 0;
        uint64_t completionFenceValue = 0;
    };

    FrameContext& GetCurrentFrameContext();
    VkCommandBuffer AcquireFrameCommandBuffer();
    VkCommandBuffer AcquireImmediateCommandBuffer();
    uint64_t SignalQueueFence();
    void WaitForQueueFence(uint64_t fenceValue);

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamilyIndex = 0;

    std::array<FrameContext, kFrameContextCount> m_frameContexts = {};
    uint32_t m_currentFrameContextIndex = kFrameContextCount - 1;

    VkCommandPool m_immediatePool = VK_NULL_HANDLE;
    VkCommandBuffer m_immediateCommandBuffer = VK_NULL_HANDLE;

    VkSemaphore m_queueTimelineSemaphore = VK_NULL_HANDLE;
    uint64_t m_nextFenceValue = 1;

    Vulkan2Functions m_functions;
};
