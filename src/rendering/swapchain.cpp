#include "swapchain.h"
#include "instance.h"
#include "utils/vulkan_utils.h"

template <VkFormat T>
Swapchain<T>::Swapchain(uint32_t width, uint32_t height, uint32_t sampleCount): m_width(width), m_height(height) {
    auto getBestSwapchainFormat = [](const std::vector<VkFormat>& applicationSupportedFormats) -> VkFormat {
        uint32_t swapchainCount = 0;
        xrEnumerateSwapchainFormats(VRManager::instance().XR->GetSession(), 0, &swapchainCount, nullptr);
        std::vector<int64_t> xrPreferredFormats(swapchainCount);
        xrEnumerateSwapchainFormats(VRManager::instance().XR->GetSession(), swapchainCount, &swapchainCount, xrPreferredFormats.data());

        auto found = std::ranges::find_first_of(xrPreferredFormats, applicationSupportedFormats);
        if (found == xrPreferredFormats.end()) {
            throw std::runtime_error("OpenXR runtime doesn't support any of the presenting modes that the OpenXR drivers support.");
        }
        return (VkFormat)*found;
    };

    const std::vector<VkFormat> preferredColorFormats = {
        // fixme: check if OpenXR prefers sRGB or not
        T
    };
    m_format = getBestSwapchainFormat(preferredColorFormats);

    XrSwapchainCreateInfo swapchainCreateInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    swapchainCreateInfo.width = width;
    swapchainCreateInfo.height = height;
    swapchainCreateInfo.arraySize = 1;
    swapchainCreateInfo.sampleCount = sampleCount;
    swapchainCreateInfo.format = m_format;
    swapchainCreateInfo.mipCount = 1;
    swapchainCreateInfo.faceCount = 1;
    swapchainCreateInfo.usageFlags = (VulkanUtils::IsDepthFormat(T) ? XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapchainCreateInfo.createFlags = 0;
    checkXRResult(xrCreateSwapchain(VRManager::instance().XR->GetSession(), &swapchainCreateInfo, &m_swapchain), "Failed to create OpenXR swapchain images!");

    uint32_t swapchainImagesCount = 0;
    checkXRResult(xrEnumerateSwapchainImages(m_swapchain, 0, &swapchainImagesCount, NULL), "Failed to enumerate swapchain images!");
    std::vector<XrSwapchainImageVulkan2KHR> swapchainImages(swapchainImagesCount, { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR });
    checkXRResult(xrEnumerateSwapchainImages(m_swapchain, swapchainImagesCount, &swapchainImagesCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchainImages.data())), "Failed to enumerate swapchain images!");

    VkDevice device = VRManager::instance().VK2->GetDevice();
    for (size_t i = 0; i < swapchainImages.size(); i++) {
        m_swapchainTextures.push_back(swapchainImages[i].image);

        VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = swapchainImages[i].image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_format;
        viewInfo.subresourceRange = {
            .aspectMask = (VkImageAspectFlags)(VulkanUtils::IsDepthFormat(T) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT),
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };
        VkImageView view;
        checkVkResult(VRManager::instance().VK2->fn().vkCreateImageView(device, &viewInfo, nullptr, &view), "Failed to create swapchain image view!");
        m_swapchainViews.push_back(view);
    }
}

template <VkFormat T>
void Swapchain<T>::PrepareRendering() {
    XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    checkXRResult(xrAcquireSwapchainImage(m_swapchain, &acquireInfo, &m_swapchainImageIdx), "Can't acquire OpenXR swapchain image!");
}

template <VkFormat T>
VkImage Swapchain<T>::StartRendering() {
    XrSwapchainImageWaitInfo waitSwapchainInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    waitSwapchainInfo.timeout = XR_INFINITE_DURATION;
    if (XrResult waitResult = xrWaitSwapchainImage(m_swapchain, &waitSwapchainInfo); waitResult == XR_TIMEOUT_EXPIRED || XR_FAILED(waitResult)) {
        checkXRResult(waitResult, "Failed to wait for swapchain image!");
    }

    return m_swapchainTextures[m_swapchainImageIdx];
}

template <VkFormat T>
void Swapchain<T>::FinishRendering() {
    XrSwapchainImageReleaseInfo releaseSwapchainInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    checkXRResult(xrReleaseSwapchainImage(m_swapchain, &releaseSwapchainInfo), "Failed to release swapchain image!");
}

template <VkFormat T>
Swapchain<T>::~Swapchain() {
    VkDevice device = VRManager::instance().VK2->GetDevice();
    for (VkImageView view : m_swapchainViews) {
        VRManager::instance().VK2->fn().vkDestroyImageView(device, view, nullptr);
    }
    if (m_swapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(m_swapchain);
    }
}

template class Swapchain<VK_FORMAT_D32_SFLOAT>;
template class Swapchain<VK_FORMAT_R8G8B8A8_UNORM>;
