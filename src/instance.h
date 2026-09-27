#pragma once

#include <pch.h>

#include "hooking/cemu_hooks.h"
#include "rendering/vulkan2.h"
#include "rendering/openxr.h"
#include "rendering/renderer.h"
#include "rendering/vulkan.h"

class VRManager {
public:
    static VRManager& instance() {
        static VRManager singletonInstance;
        return singletonInstance;
    }

    VRManager(VRManager const&) = delete;
    void operator=(VRManager const&) = delete;

    void Init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) {
        VK = std::make_unique<RND_Vulkan>(instance, physicalDevice, device);
        Log::print<INFO>("Initialized VRManager instance...");
    }

    void InitSession() {
        XrGraphicsBindingVulkan2KHR vkBinding = { XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR };
        vkBinding.instance = VK2->GetInstance();
        vkBinding.physicalDevice = VK2->GetPhysicalDevice();
        vkBinding.device = VK2->GetDevice();
        vkBinding.queueFamilyIndex = VK2->GetQueueFamilyIndex();
        vkBinding.queueIndex = 0;
        XR->CreateSession(vkBinding);
        XR->CreateActions();
        Hooks = std::make_unique<CemuHooks>();
    }

    std::unique_ptr<OpenXR> XR;
    std::unique_ptr<RND_Vulkan2> VK2;
    std::unique_ptr<RND_Vulkan> VK;
    std::unique_ptr<CemuHooks> Hooks;

    uint32_t vkVersion = 0;

private:
    VRManager() {
        m_logger = std::make_unique<Log>();
        XR = std::make_unique<OpenXR>();
        // Constructed here (not in Init(), which only runs once Cemu's own vkCreateDevice
        // hook fires) because Cemu's vkEnumeratePhysicalDevices hook - which runs earlier,
        // right after vkCreateInstance - needs Device B's already-selected physical device
        // to match Cemu's own GPU against it by UUID. XR is already fully constructed
        // above, so passing its instance/system directly here (rather than reaching back
        // through VRManager::instance()) avoids recursing into this constructor while its
        // own Meyer's-singleton static-init is still in progress.
        VK2 = std::make_unique<RND_Vulkan2>(XR->GetXrInstance(), XR->GetSystemId());
    };

    ~VRManager() {
        // note: OpenXR gets to remove its swapchains first before Device B gets destroyed, so reverse that order
        VK.reset();
        XR.reset();
        VK2.reset();

        m_logger.reset();
    };

    std::unique_ptr<Log> m_logger;
};
