#include "pch.h"

#include "vulkan2.h"
#include "instance.h"
#include "utils/vulkan_utils.h"
#include "shader.h"
#include "shader_spirv.h"
#include <dlfcn.h>

void Vulkan2Functions::Load(PFN_vkGetInstanceProcAddr instanceLoad, VkInstance instance, VkDevice device) {
    vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)instanceLoad(instance, "vkGetDeviceProcAddr");
    checkAssert(vkGetDeviceProcAddr != nullptr, "Failed to load vkGetDeviceProcAddr for Device B!");

#define VK2_LOAD_DEV(name) name = (PFN_##name)vkGetDeviceProcAddr(device, #name); checkAssert(name != nullptr, "Failed to load " #name " for Device B!")
    VK2_LOAD_DEV(vkDestroyImage);
    VK2_LOAD_DEV(vkCreateImage);
    VK2_LOAD_DEV(vkGetImageMemoryRequirements);
    VK2_LOAD_DEV(vkAllocateMemory);
    VK2_LOAD_DEV(vkFreeMemory);
    VK2_LOAD_DEV(vkBindImageMemory);
    VK2_LOAD_DEV(vkCreateImageView);
    VK2_LOAD_DEV(vkDestroyImageView);
    VK2_LOAD_DEV(vkCreateSemaphore);
    VK2_LOAD_DEV(vkDestroySemaphore);
    VK2_LOAD_DEV(vkImportSemaphoreFdKHR);
    VK2_LOAD_DEV(vkGetSemaphoreFdKHR);
    VK2_LOAD_DEV(vkGetMemoryFdKHR);
    VK2_LOAD_DEV(vkQueueSubmit);
    VK2_LOAD_DEV(vkQueueWaitIdle);
    VK2_LOAD_DEV(vkWaitSemaphores);
    VK2_LOAD_DEV(vkCmdPipelineBarrier);
    VK2_LOAD_DEV(vkBeginCommandBuffer);
    VK2_LOAD_DEV(vkEndCommandBuffer);
    VK2_LOAD_DEV(vkCreateCommandPool);
    VK2_LOAD_DEV(vkDestroyCommandPool);
    VK2_LOAD_DEV(vkAllocateCommandBuffers);
    VK2_LOAD_DEV(vkFreeCommandBuffers);
    VK2_LOAD_DEV(vkResetCommandPool);
    VK2_LOAD_DEV(vkCreateBuffer);
    VK2_LOAD_DEV(vkDestroyBuffer);
    VK2_LOAD_DEV(vkGetBufferMemoryRequirements);
    VK2_LOAD_DEV(vkBindBufferMemory);
    VK2_LOAD_DEV(vkMapMemory);
    VK2_LOAD_DEV(vkUnmapMemory);
    VK2_LOAD_DEV(vkCreateDescriptorSetLayout);
    VK2_LOAD_DEV(vkCreatePipelineLayout);
    VK2_LOAD_DEV(vkCreateSampler);
    VK2_LOAD_DEV(vkCreateDescriptorPool);
    VK2_LOAD_DEV(vkAllocateDescriptorSets);
    VK2_LOAD_DEV(vkUpdateDescriptorSets);
    VK2_LOAD_DEV(vkCreateShaderModule);
    VK2_LOAD_DEV(vkDestroyShaderModule);
    VK2_LOAD_DEV(vkCreateGraphicsPipelines);
    VK2_LOAD_DEV(vkDestroyPipeline);
    VK2_LOAD_DEV(vkCmdBeginRendering);
    VK2_LOAD_DEV(vkCmdEndRendering);
    VK2_LOAD_DEV(vkCmdBindPipeline);
    VK2_LOAD_DEV(vkCmdBindDescriptorSets);
    VK2_LOAD_DEV(vkCmdSetViewport);
    VK2_LOAD_DEV(vkCmdSetScissor);
    VK2_LOAD_DEV(vkCmdDraw);
    VK2_LOAD_DEV(vkGetDeviceQueue);
#undef VK2_LOAD_DEV

#define VK2_LOAD_INST(name) name = (PFN_##name)instanceLoad(instance, #name); checkAssert(name != nullptr, "Failed to load " #name " for Device B!")
    VK2_LOAD_INST(vkGetPhysicalDeviceMemoryProperties);
    VK2_LOAD_INST(vkGetPhysicalDeviceQueueFamilyProperties);
    VK2_LOAD_INST(vkGetPhysicalDeviceProperties);
#undef VK2_LOAD_INST
}

static uint32_t FindQueueFamily(PFN_vkGetPhysicalDeviceQueueFamilyProperties getQueueFamilyProps, VkPhysicalDevice phys) {
    uint32_t count = 0;
    getQueueFamilyProps(phys, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    getQueueFamilyProps(phys, &count, families.data());
    for (uint32_t i = 0; i < count; i++) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) return i;
    }
    checkAssert(false, "No graphics queue family found on Device B!");
    return 0;
}

RND_Vulkan2::RND_Vulkan2(XrInstance xrInstance, XrSystemId systemId) {
    // pch.h's VK_NO_PROTOTYPES means "vkGetInstanceProcAddr" isn't a declared,
    // linkable symbol here - fetch the real one via dlopen/dlsym instead. Deliberately
    // not linking this layer .so against -lvulkan directly and calling functions
    // unprefixed instead - this layer also EXPORTS functions with the exact same
    // Vulkan-mandated names (its own vkCreateInstance hook, etc.), and risking symbol
    // interposition resolving a "real" call back into our own hook is not a risk
    // worth taking.
    //
    // Deliberately NOT using dlsym(RTLD_DEFAULT, ...) here: inside a small standalone
    // test binary that's an unambiguous way to reach the real loader, but inside Cemu's
    // actual process RTLD_DEFAULT searches every loaded library's global symbols in
    // load order, which can include the GPU driver's own ICD (e.g. libvulkan_radeon.so)
    // or another active layer - anything else exporting a symbol literally named
    // vkGetInstanceProcAddr. That was silently landing on the wrong implementation here:
    // Device B's own instance/device calls worked either way, but WiVRn's internal
    // vulkan_enable2 state (populated via the callback we hand it) came out uninitialized,
    // making xrGetVulkanGraphicsDevice2KHR fail with XR_ERROR_VALIDATION_FAILURE
    // (confirmed reproducing 100% of the time in real Cemu, while the standalone
    // vk-vk-openxr-test - which links -lvulkan directly, no ambiguity - always worked).
    // dlopen-ing the loader's actual SONAME and resolving the symbol from that specific
    // handle removes the ambiguity: it's guaranteed to be the real Khronos loader, never
    // an ICD or another layer, and since libvulkan.so.1 is already loaded in this process
    // (Cemu itself links against it) this just bumps its refcount rather than loading
    // anything new.
    void* vulkanLoaderHandle = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_NOLOAD);
    if (vulkanLoaderHandle == nullptr) {
        // Fallback for the (unexpected) case where it's not already loaded under that
        // exact SONAME - actually load it rather than just probing.
        vulkanLoaderHandle = dlopen("libvulkan.so.1", RTLD_NOW);
    }
    checkAssert(vulkanLoaderHandle != nullptr, "Failed to dlopen libvulkan.so.1 for Device B!");
    auto realGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(vulkanLoaderHandle, "vkGetInstanceProcAddr");
    checkAssert(realGetInstanceProcAddr != nullptr, "Failed to find vkGetInstanceProcAddr in libvulkan.so.1!");

    PFN_xrGetVulkanGraphicsRequirements2KHR xrGetVulkanGraphicsRequirements2KHR_ = nullptr;
    PFN_xrCreateVulkanInstanceKHR xrCreateVulkanInstanceKHR_ = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR xrGetVulkanGraphicsDevice2KHR_ = nullptr;
    PFN_xrCreateVulkanDeviceKHR xrCreateVulkanDeviceKHR_ = nullptr;
    checkXRResult(xrGetInstanceProcAddr(xrInstance, "xrGetVulkanGraphicsRequirements2KHR", (PFN_xrVoidFunction*)&xrGetVulkanGraphicsRequirements2KHR_), "Failed to load xrGetVulkanGraphicsRequirements2KHR!");
    checkXRResult(xrGetInstanceProcAddr(xrInstance, "xrCreateVulkanInstanceKHR", (PFN_xrVoidFunction*)&xrCreateVulkanInstanceKHR_), "Failed to load xrCreateVulkanInstanceKHR!");
    checkXRResult(xrGetInstanceProcAddr(xrInstance, "xrGetVulkanGraphicsDevice2KHR", (PFN_xrVoidFunction*)&xrGetVulkanGraphicsDevice2KHR_), "Failed to load xrGetVulkanGraphicsDevice2KHR!");
    checkXRResult(xrGetInstanceProcAddr(xrInstance, "xrCreateVulkanDeviceKHR", (PFN_xrVoidFunction*)&xrCreateVulkanDeviceKHR_), "Failed to load xrCreateVulkanDeviceKHR!");

    XrGraphicsRequirementsVulkan2KHR vkReqs = { XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR };
    checkXRResult(xrGetVulkanGraphicsRequirements2KHR_(xrInstance, systemId, &vkReqs), "Failed to get Vulkan graphics requirements from OpenXR!");

    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "BetterVR_Layer_DeviceB";
    // 1.3, not 1.2: VK_KHR_dynamic_rendering (vkCmdBeginRendering/vkCmdEndRendering,
    // used by PresentPipeline) was only promoted to core in Vulkan 1.3. At apiVersion
    // 1.2 the driver has no obligation to resolve the unsuffixed core name via
    // vkGetDeviceProcAddr even with the KHR extension enabled - confirmed failing
    // exactly that way on RADV. 1.3 is safe here regardless of what apiVersion Cemu's
    // own instance (Device A) happens to use - these are two entirely separate
    // VkInstances - and WiVRn's own requirements check reports a range starting at 1.0.
    appInfo.apiVersion = VK_API_VERSION_1_3;
    std::vector<const char*> instExts = {
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
    VkInstanceCreateInfo instInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instInfo.pApplicationInfo = &appInfo;
    instInfo.enabledExtensionCount = (uint32_t)instExts.size();
    instInfo.ppEnabledExtensionNames = instExts.data();

    XrVulkanInstanceCreateInfoKHR xrVkInstInfo = { XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR };
    xrVkInstInfo.systemId = systemId;
    xrVkInstInfo.pfnGetInstanceProcAddr = realGetInstanceProcAddr;
    xrVkInstInfo.vulkanCreateInfo = &instInfo;
    VkResult vkInstResult = VK_SUCCESS;
    checkXRResult(xrCreateVulkanInstanceKHR_(xrInstance, &xrVkInstInfo, &m_instance, &vkInstResult), "xrCreateVulkanInstanceKHR failed!");
    checkVkResult(vkInstResult, "xrCreateVulkanInstanceKHR's inner vkCreateInstance failed!");

    // m_instance was created via xrCreateVulkanInstanceKHR above, so (unlike passing
    // Cemu's own instance, which this function does NOT accept despite its "2KHR" name
    // resembling the older, more permissive xrGetVulkanGraphicsDeviceKHR) this is exactly
    // the instance xrGetVulkanGraphicsDevice2KHR expects.
    XrVulkanGraphicsDeviceGetInfoKHR xrVkDeviceGetInfo = { XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };
    xrVkDeviceGetInfo.systemId = systemId;
    xrVkDeviceGetInfo.vulkanInstance = m_instance;
    checkXRResult(xrGetVulkanGraphicsDevice2KHR_(xrInstance, &xrVkDeviceGetInfo, &m_physicalDevice), "xrGetVulkanGraphicsDevice2KHR failed!");

    // Only instance-level functions exist before the device is created - load just
    // those now (m_functions.Load() below reloads them, harmlessly, along with
    // everything device-level once m_device exists).
    m_functions.vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)realGetInstanceProcAddr(m_instance, "vkGetPhysicalDeviceProperties");
    m_functions.vkGetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2)realGetInstanceProcAddr(m_instance, "vkGetPhysicalDeviceProperties2");
    m_functions.vkGetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)realGetInstanceProcAddr(m_instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    checkAssert(m_functions.vkGetPhysicalDeviceProperties != nullptr && m_functions.vkGetPhysicalDeviceProperties2 != nullptr && m_functions.vkGetPhysicalDeviceQueueFamilyProperties != nullptr, "Failed to load early instance-level Vulkan functions for Device B!");

    VkPhysicalDeviceProperties props{};
    m_functions.vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    Log::print<INFO>("Device B (OpenXR-selected GPU): {} (vendor={:#06x}, device={:#06x})", props.deviceName, props.vendorID, props.deviceID);

    m_queueFamilyIndex = FindQueueFamily(m_functions.vkGetPhysicalDeviceQueueFamilyProperties, m_physicalDevice);
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueInfo.queueFamilyIndex = m_queueFamilyIndex;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES };
    timelineFeatures.timelineSemaphore = VK_TRUE;
    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
    dynamicRenderingFeatures.pNext = &timelineFeatures;
    dynamicRenderingFeatures.dynamicRendering = VK_TRUE;

    std::vector<const char*> devExts = {
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME, // dependency of dynamic_rendering on some drivers
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,   // dependency of depth_stencil_resolve
    };
    VkDeviceCreateInfo devInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    devInfo.pNext = &dynamicRenderingFeatures;
    devInfo.queueCreateInfoCount = 1;
    devInfo.pQueueCreateInfos = &queueInfo;
    devInfo.enabledExtensionCount = (uint32_t)devExts.size();
    devInfo.ppEnabledExtensionNames = devExts.data();

    XrVulkanDeviceCreateInfoKHR xrVkDevInfo = { XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR };
    xrVkDevInfo.systemId = systemId;
    xrVkDevInfo.pfnGetInstanceProcAddr = realGetInstanceProcAddr;
    xrVkDevInfo.vulkanPhysicalDevice = m_physicalDevice;
    xrVkDevInfo.vulkanCreateInfo = &devInfo;
    VkResult vkDevResult = VK_SUCCESS;
    checkXRResult(xrCreateVulkanDeviceKHR_(xrInstance, &xrVkDevInfo, &m_device, &vkDevResult), "xrCreateVulkanDeviceKHR failed!");
    checkVkResult(vkDevResult, "xrCreateVulkanDeviceKHR's inner vkCreateDevice failed!");

    m_functions.Load(realGetInstanceProcAddr, m_instance, m_device);
    m_functions.vkGetDeviceQueue(m_device, m_queueFamilyIndex, 0, &m_queue);
    Log::print<INFO>("Device B created successfully via XR_KHR_vulkan_enable2");

    // Frame + immediate command pools/buffers
    VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamilyIndex;

    for (auto& frameContext : m_frameContexts) {
        checkVkResult(m_functions.vkCreateCommandPool(m_device, &poolInfo, nullptr, &frameContext.pool), "Failed to create Device B frame command pool!");
        VkCommandBufferAllocateInfo cmdAllocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cmdAllocInfo.commandPool = frameContext.pool;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount = kFrameCommandBufferCount;
        checkVkResult(m_functions.vkAllocateCommandBuffers(m_device, &cmdAllocInfo, frameContext.commandBuffers.data()), "Failed to allocate Device B frame command buffers!");
    }

    checkVkResult(m_functions.vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_immediatePool), "Failed to create Device B immediate command pool!");
    VkCommandBufferAllocateInfo immediateAllocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    immediateAllocInfo.commandPool = m_immediatePool;
    immediateAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    immediateAllocInfo.commandBufferCount = 1;
    checkVkResult(m_functions.vkAllocateCommandBuffers(m_device, &immediateAllocInfo, &m_immediateCommandBuffer), "Failed to allocate Device B immediate command buffer!");

    VkSemaphoreTypeCreateInfo timelineCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineCreateInfo.initialValue = 0;
    VkSemaphoreCreateInfo semInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    semInfo.pNext = &timelineCreateInfo;
    checkVkResult(m_functions.vkCreateSemaphore(m_device, &semInfo, nullptr, &m_queueTimelineSemaphore), "Failed to create Device B queue timeline semaphore!");
}

RND_Vulkan2::~RND_Vulkan2() {
    // Deliberately minimal teardown - this mod's overall process lifetime is tied to
    // Cemu's, and getting shutdown ordering exactly right across two devices plus
    // OpenXR is its own separate piece of work, not needed to prove the core
    // rendering path works.
}

uint32_t RND_Vulkan2::FindMemoryType(uint32_t memoryTypeBitsRequirement, VkMemoryPropertyFlags requirementsMask) {
    VkPhysicalDeviceMemoryProperties memProps;
    m_functions.vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memoryTypeBitsRequirement & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & requirementsMask) == requirementsMask) {
            return i;
        }
    }
    checkAssert(false, "Failed to find suitable memory type on Device B");
    return 0;
}

std::array<uint8_t, VK_UUID_SIZE> RND_Vulkan2::GetPhysicalDeviceUUID() const {
    VkPhysicalDeviceIDProperties idProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
    VkPhysicalDeviceProperties2 props2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &idProps };
    m_functions.vkGetPhysicalDeviceProperties2(m_physicalDevice, &props2);
    std::array<uint8_t, VK_UUID_SIZE> uuid;
    std::memcpy(uuid.data(), idProps.deviceUUID, VK_UUID_SIZE);
    return uuid;
}

void RND_Vulkan2::StartFrame() {
    m_currentFrameContextIndex = (m_currentFrameContextIndex + 1) % kFrameContextCount;

    FrameContext& frameContext = GetCurrentFrameContext();
    if (frameContext.completionFenceValue != 0) {
        WaitForQueueFence(frameContext.completionFenceValue);
        frameContext.completionFenceValue = 0;
    }

    checkVkResult(m_functions.vkResetCommandPool(m_device, frameContext.pool, 0), "Failed to reset Device B frame command pool!");
    frameContext.nextCommandBufferIndex = 0;
}

void RND_Vulkan2::EndFrame() {
    GetCurrentFrameContext().completionFenceValue = SignalQueueFence();
}

RND_Vulkan2::FrameContext& RND_Vulkan2::GetCurrentFrameContext() {
    return m_frameContexts[m_currentFrameContextIndex];
}

VkCommandBuffer RND_Vulkan2::AcquireFrameCommandBuffer() {
    FrameContext& frameContext = GetCurrentFrameContext();
    checkAssert(frameContext.nextCommandBufferIndex < frameContext.commandBuffers.size(), "Exceeded the reusable Device B frame command buffer pool!");
    return frameContext.commandBuffers[frameContext.nextCommandBufferIndex++];
}

VkCommandBuffer RND_Vulkan2::AcquireImmediateCommandBuffer() {
    checkVkResult(m_functions.vkResetCommandPool(m_device, m_immediatePool, 0), "Failed to reset Device B immediate command pool!");
    return m_immediateCommandBuffer;
}

uint64_t RND_Vulkan2::SignalQueueFence() {
    const uint64_t fenceValue = m_nextFenceValue++;
    uint64_t signalValue = fenceValue;
    VkTimelineSemaphoreSubmitInfo timelineInfo = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &signalValue;
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.pNext = &timelineInfo;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &m_queueTimelineSemaphore;
    checkVkResult(m_functions.vkQueueSubmit(m_queue, 1, &submit, VK_NULL_HANDLE), "Failed to signal Device B queue fence!");
    return fenceValue;
}

void RND_Vulkan2::WaitForQueueFence(uint64_t fenceValue) {
    uint64_t value = fenceValue;
    VkSemaphoreWaitInfo waitInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &m_queueTimelineSemaphore;
    waitInfo.pValues = &value;
    checkVkResult(m_functions.vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX), "Failed to wait for Device B queue fence!");
}

// ===========================================================================
// PresentPipeline: composites the shared texture (+ fade sample) into a
// swapchain image, via dynamic rendering (no VkRenderPass/VkFramebuffer
// bureaucracy - maps directly onto the D3D12 original's RTV-descriptor model).
// ===========================================================================

RND_Vulkan2::PresentPipeline::PresentPipeline(RND_Renderer* pRenderer): m_renderer(pRenderer) {
    VkDevice device = VRManager::instance().VK2->GetDevice();

    VkDescriptorSetLayoutBinding bindings[3] = {};
    bindings[0] = { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT };
    bindings[1] = { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT };
    bindings[2] = { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT };

    VkDescriptorSetLayoutCreateInfo setLayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    setLayoutInfo.bindingCount = 3;
    setLayoutInfo.pBindings = bindings;
    checkVkResult(VRManager::instance().VK2->fn().vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &m_descriptorSetLayout), "Failed to create present pipeline descriptor set layout!");

    VkPipelineLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;
    checkVkResult(VRManager::instance().VK2->fn().vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout), "Failed to create present pipeline layout!");

    // LINEAR, not NEAREST: g_colorTexture (binding 0) is the composited game+HUD frame,
    // magnified from its captured render resolution up to the much larger per-eye
    // swapchain size, so nearest-neighbor here reads as a blocky/pixelated image in the
    // headset. g_fadeSampleTexture (binding 1) only ever uses texelFetch in the shader,
    // which ignores the sampler's filter mode entirely, so this change only affects the
    // actual displayed frame.
    VkSamplerCreateInfo samplerInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    checkVkResult(VRManager::instance().VK2->fn().vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler), "Failed to create present pipeline sampler!");

    VkDescriptorPoolSize poolSizes[2] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
    };
    VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    checkVkResult(VRManager::instance().VK2->fn().vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool), "Failed to create present pipeline descriptor pool!");

    VkDescriptorSetAllocateInfo dsAllocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsAllocInfo.descriptorPool = m_descriptorPool;
    dsAllocInfo.descriptorSetCount = 1;
    dsAllocInfo.pSetLayouts = &m_descriptorSetLayout;
    checkVkResult(VRManager::instance().VK2->fn().vkAllocateDescriptorSets(device, &dsAllocInfo, &m_descriptorSet), "Failed to allocate present pipeline descriptor set!");

    // Settings uniform buffer, matching struct presentSettings (shader.h) layout
    VkBufferCreateInfo bufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufInfo.size = sizeof(presentSettings);
    bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkVkResult(VRManager::instance().VK2->fn().vkCreateBuffer(device, &bufInfo, nullptr, &m_settingsBuffer), "Failed to create present pipeline settings buffer!");

    VkMemoryRequirements memReqs;
    VRManager::instance().VK2->fn().vkGetBufferMemoryRequirements(device, m_settingsBuffer, &memReqs);
    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = VRManager::instance().VK2->FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    checkVkResult(VRManager::instance().VK2->fn().vkAllocateMemory(device, &allocInfo, nullptr, &m_settingsMemory), "Failed to allocate present pipeline settings memory!");
    checkVkResult(VRManager::instance().VK2->fn().vkBindBufferMemory(device, m_settingsBuffer, m_settingsMemory, 0), "Failed to bind present pipeline settings memory!");
    checkVkResult(VRManager::instance().VK2->fn().vkMapMemory(device, m_settingsMemory, 0, sizeof(presentSettings), 0, &m_settingsMapped), "Failed to map present pipeline settings memory!");

    VkDescriptorBufferInfo bufferInfo = { m_settingsBuffer, 0, sizeof(presentSettings) };
    VkWriteDescriptorSet settingsWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    settingsWrite.dstSet = m_descriptorSet;
    settingsWrite.dstBinding = 2;
    settingsWrite.descriptorCount = 1;
    settingsWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    settingsWrite.pBufferInfo = &bufferInfo;
    VRManager::instance().VK2->fn().vkUpdateDescriptorSets(device, 1, &settingsWrite, 0, nullptr);
}

RND_Vulkan2::PresentPipeline::~PresentPipeline() {
    // See RND_Vulkan2::~RND_Vulkan2 - minimal teardown, process-lifetime-bound.
}

void RND_Vulkan2::PresentPipeline::EnsurePipeline(VkFormat colorFormat) {
    if (m_pipeline != VK_NULL_HANDLE && m_pipelineColorFormat == colorFormat) {
        return;
    }
    m_pipelineColorFormat = colorFormat;

    VkDevice device = VRManager::instance().VK2->GetDevice();
    if (m_pipeline != VK_NULL_HANDLE) {
        VRManager::instance().VK2->fn().vkDestroyPipeline(device, m_pipeline, nullptr);
    }

    VkShaderModuleCreateInfo vertModuleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    vertModuleInfo.codeSize = presentVertSpirv_size;
    vertModuleInfo.pCode = presentVertSpirv;
    VkShaderModule vertModule;
    checkVkResult(VRManager::instance().VK2->fn().vkCreateShaderModule(device, &vertModuleInfo, nullptr, &vertModule), "Failed to create present vertex shader module!");

    VkShaderModuleCreateInfo fragModuleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    fragModuleInfo.codeSize = presentFragSpirv_size;
    fragModuleInfo.pCode = presentFragSpirv;
    VkShaderModule fragModule;
    checkVkResult(VRManager::instance().VK2->fn().vkCreateShaderModule(device, &fragModuleInfo, nullptr, &fragModule), "Failed to create present fragment shader module!");

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" };
    stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" };

    VkPipelineVertexInputStateCreateInfo vertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    // 4 vertices, no index/vertex buffer (positions generated in the vertex shader
    // from gl_VertexIndex) - a strip covers the full quad in one draw; culling is
    // off so the strip's alternating winding doesn't matter.
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment = {};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blendState = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    blendState.attachmentCount = 1;
    blendState.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo renderingInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &blendState;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    checkVkResult(VRManager::instance().VK2->fn().vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline), "Failed to create present graphics pipeline!");

    VRManager::instance().VK2->fn().vkDestroyShaderModule(device, vertModule, nullptr);
    VRManager::instance().VK2->fn().vkDestroyShaderModule(device, fragModule, nullptr);
}

void RND_Vulkan2::PresentPipeline::BindAttachment(uint32_t attachmentIdx, VkImageView srcView) {
    if (attachmentIdx == 0) {
        m_attachmentView = srcView;
        m_descriptorSetDirty = true;
    }
}

void RND_Vulkan2::PresentPipeline::BindFadeSample(VkImageView fadeView) {
    m_fadeSampleView = fadeView;
    m_descriptorSetDirty = true;
}

void RND_Vulkan2::PresentPipeline::UpdateDescriptorSet() {
    if (!m_descriptorSetDirty || m_attachmentView == VK_NULL_HANDLE || m_fadeSampleView == VK_NULL_HANDLE) {
        return;
    }
    m_descriptorSetDirty = false;

    VkDescriptorImageInfo colorImageInfo = { m_sampler, m_attachmentView, VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorImageInfo fadeImageInfo = { m_sampler, m_fadeSampleView, VK_IMAGE_LAYOUT_GENERAL };

    VkWriteDescriptorSet writes[2] = {};
    writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    writes[0].dstSet = m_descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &colorImageInfo;
    writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    writes[1].dstSet = m_descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &fadeImageInfo;
    VRManager::instance().VK2->fn().vkUpdateDescriptorSets(VRManager::instance().VK2->GetDevice(), 2, writes, 0, nullptr);
}

void RND_Vulkan2::PresentPipeline::BindSettings(float screenWidth, float screenHeight, const RenderUtils::UvTransform& uvTransform) {
    m_renderWidth = screenWidth;
    m_renderHeight = screenHeight;
    m_uvTransform = uvTransform;
}

void RND_Vulkan2::PresentPipeline::UpdateSettingsBuffer(uint32_t swapchainWidth, uint32_t swapchainHeight) {
    const RND_Renderer::CustomFade fade = m_renderer != nullptr ? m_renderer->GetCustomFade() : RND_Renderer::CustomFade{};
    presentSettings settings = {
        .renderWidth = m_renderWidth,
        .renderHeight = m_renderHeight,
        .swapchainWidth = (float)swapchainWidth,
        .swapchainHeight = (float)swapchainHeight,
        .uvOffsetX = m_uvTransform.offsetX,
        .uvOffsetY = m_uvTransform.offsetY,
        .uvScaleX = m_uvTransform.scaleX,
        .uvScaleY = m_uvTransform.scaleY,
        .customFadeAmount = fade.amount,
        .customFadeColorR = fade.color.x,
        .customFadeColorG = fade.color.y,
        .customFadeColorB = fade.color.z,
        .isFadeActive = (m_renderer != nullptr && m_renderer->IsFadeActive()) ? 1.0f : 0.0f,
    };
    memcpy(m_settingsMapped, &settings, sizeof(settings));
}

void RND_Vulkan2::PresentPipeline::Render(VkCommandBuffer cmdBuffer, VkImage dstImage, VkImageView dstView, VkFormat dstFormat, uint32_t width, uint32_t height) {
    EnsurePipeline(dstFormat);
    UpdateDescriptorSet();
    UpdateSettingsBuffer(width, height);

    VkRenderingAttachmentInfo colorAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAttachment.imageView = dstView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderingInfo.renderArea = { { 0, 0 }, { width, height } };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    VRManager::instance().VK2->fn().vkCmdBeginRendering(cmdBuffer, &renderingInfo);

    VRManager::instance().VK2->fn().vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    VRManager::instance().VK2->fn().vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    VkViewport viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    VkRect2D scissor = { { 0, 0 }, { width, height } };
    VRManager::instance().VK2->fn().vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
    VRManager::instance().VK2->fn().vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

    // full-screen quad, no vertex buffer - matches the original screenIndices draw
    VRManager::instance().VK2->fn().vkCmdDraw(cmdBuffer, 4, 1, 0, 0);

    VRManager::instance().VK2->fn().vkCmdEndRendering(cmdBuffer);
}
