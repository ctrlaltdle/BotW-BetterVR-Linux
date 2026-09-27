#pragma once

namespace VRLayer {
    void LogDeviceFaultInfo();

    // Device B (RND_Vulkan2) is a second, independent VkInstance/VkDevice, but since it's
    // created via the real vkGetInstanceProcAddr (see vulkan2.cpp), every globally-active
    // Vulkan layer - this one included - applies to it too, same as any other Vulkan app.
    // None of this layer's hooks are meant to apply to its OWN internal presenter device:
    // beyond being pointless, some of them actively break it (recursively re-entering the
    // still-under-construction VRManager singleton; deadlocking on lockImageResolutions
    // when Device B's own swapchain image creation reenters CreateImage from inside the
    // framebuffer.cpp code that's already holding that same mutex while constructing
    // Layer3D/Layer2D - both confirmed live against a real Quest 3+WiVRn). Every
    // VkInstanceOverrides/VkDeviceOverrides hook below checks these first and passes
    // straight through for Device B.
    bool IsDeviceBInstance(VkInstance instance);
    bool IsDeviceBDevice(VkDevice device);

    class VkInstanceOverrides {
    public:
        static VkResult CreateInstance(PFN_vkCreateInstance createInstanceFunc, const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance);
        static void DestroyInstance(const vkroots::VkInstanceDispatch& pDispatch, VkInstance instance, const VkAllocationCallbacks* pAllocator);

        static VkResult EnumeratePhysicalDevices(const vkroots::VkInstanceDispatch& pDispatch, VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices);
        static void GetPhysicalDeviceProperties(const vkroots::VkPhysicalDeviceDispatch& pDispatch, VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties);
        static void GetPhysicalDeviceQueueFamilyProperties(const vkroots::VkPhysicalDeviceDispatch& pDispatch, VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties);

        static VkResult CreateDevice(const vkroots::VkPhysicalDeviceDispatch& pDispatch, VkPhysicalDevice gpu, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
    };


    class VkDeviceOverrides {
    public:
        static void DestroyDevice(const vkroots::VkDeviceDispatch& pDispatch, VkDevice device, const VkAllocationCallbacks* pAllocator);

        // Overrides used for finding framebuffer
        static VkResult CreateImage(const vkroots::VkDeviceDispatch& pDispatch, VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage);
        static void DestroyImage(const vkroots::VkDeviceDispatch& pDispatch, VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator);
        static void CmdClearColorImage(const vkroots::VkCommandBufferDispatch& pDispatch, VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges);
        static void CmdClearDepthStencilImage(const vkroots::VkCommandBufferDispatch& pDispatch, VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges);
        static VkResult QueuePresentKHR(const vkroots::VkQueueDispatch& pDispatch, VkQueue queue, const VkPresentInfoKHR* pPresentInfo);


        // frame manager
        static VkResult CreateSwapchainKHR(const vkroots::VkDeviceDispatch& pDispatch, VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);
        static VkResult QueueSubmit(const vkroots::VkQueueDispatch& pDispatch, VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);
    };
}