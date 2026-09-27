#include "texture.h"
#include "instance.h"
#include "utils/vulkan_utils.h"


BaseVulkanTexture::~BaseVulkanTexture() {
    if (m_vkImage != VK_NULL_HANDLE) {
        VRManager::instance().VK->GetDeviceDispatch()->DestroyImage(VRManager::instance().VK->GetDevice(), m_vkImage, nullptr);
        m_vkImage = VK_NULL_HANDLE;
    }
    if (m_vkMemory != VK_NULL_HANDLE) {
        VRManager::instance().VK->GetDeviceDispatch()->FreeMemory(VRManager::instance().VK->GetDevice(), m_vkMemory, nullptr);
        m_vkMemory = VK_NULL_HANDLE;
    }
}

void BaseVulkanTexture::vkPipelineBarrier(VkCommandBuffer cmdBuffer) {
    return VulkanUtils::DebugPipelineBarrier(cmdBuffer);
}

VkImageAspectFlags BaseVulkanTexture::GetAspectMask() const {
    return VulkanUtils::GetAspectMaskForFormat(m_vkFormat);
}

void BaseVulkanTexture::vkTransitionLayout(VkCommandBuffer cmdBuffer, VkImageLayout newLayout) {
    VulkanUtils::DebugPipelineBarrier(cmdBuffer);
    VulkanUtils::TransitionLayout(cmdBuffer, m_vkImage, m_vkCurrLayout, newLayout, GetAspectMask());
    VulkanUtils::DebugPipelineBarrier(cmdBuffer);
    m_vkCurrLayout = newLayout;
}

void BaseVulkanTexture::vkCopyToImage(VkCommandBuffer cmdBuffer, VkImage dstImage) {
    auto* dispatch = VRManager::instance().VK->GetDeviceDispatch();
    VkImageAspectFlags aspectMask = GetAspectMask();

    const VkImageCopy region = {
        .srcSubresource = { aspectMask, 0, 0, 1 },
        .srcOffset = { 0, 0, 0 },
        .dstSubresource = { aspectMask, 0, 0, 1 },
        .dstOffset = { 0, 0, 0 },
        .extent = {
            .width = m_width,
            .height = m_height,
            .depth = 1
        }
    };

    dispatch->CmdCopyImage(cmdBuffer, m_vkImage, m_vkCurrLayout, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // AMD GPU FIX: Add memory barrier after copy to ensure data is visible
    VkMemoryBarrier postCopyBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    postCopyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    postCopyBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    dispatch->CmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &postCopyBarrier, 0, nullptr, 0, nullptr);
}

void BaseVulkanTexture::vkClear(VkCommandBuffer cmdBuffer, VkClearColorValue color) {
    auto* dispatch = VRManager::instance().VK->GetDeviceDispatch();

    // Only use CmdClearColorImage for color images
    if (VulkanUtils::IsDepthFormat(m_vkFormat)) {
        Log::print<WARNING>("vkClear called on depth image - use vkClearDepth instead");
        return;
    }

    if (m_vkCurrLayout != VK_IMAGE_LAYOUT_GENERAL && m_vkCurrLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        VulkanUtils::TransitionLayout(cmdBuffer, m_vkImage, m_vkCurrLayout, VK_IMAGE_LAYOUT_GENERAL);
        m_vkCurrLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    const VkImageSubresourceRange range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS
    };

    VulkanUtils::DebugPipelineBarrier(cmdBuffer);
    dispatch->CmdClearColorImage(cmdBuffer, m_vkImage, m_vkCurrLayout, &color, 1, &range);
    VulkanUtils::DebugPipelineBarrier(cmdBuffer);
}

void BaseVulkanTexture::vkClearDepth(VkCommandBuffer cmdBuffer, float depth, uint32_t stencil) {
    auto* dispatch = VRManager::instance().VK->GetDeviceDispatch();

    if (!VulkanUtils::IsDepthFormat(m_vkFormat)) {
        Log::print<WARNING>("vkClearDepth called on color image - use vkClear instead");
        return;
    }

    // AMD GPU FIX: Transition to GENERAL if not already in a valid clear layout
    // CmdClearDepthStencilImage requires GENERAL or TRANSFER_DST_OPTIMAL
    if (m_vkCurrLayout != VK_IMAGE_LAYOUT_GENERAL && m_vkCurrLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        VulkanUtils::TransitionLayout(cmdBuffer, m_vkImage, m_vkCurrLayout, VK_IMAGE_LAYOUT_GENERAL, GetAspectMask());
        m_vkCurrLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    VkClearDepthStencilValue clearValue = {
        .depth = depth,
        .stencil = stencil
    };

    const VkImageSubresourceRange range = {
        .aspectMask = GetAspectMask(),
        .baseMipLevel = 0,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS
    };

    dispatch->CmdClearDepthStencilImage(cmdBuffer, m_vkImage, m_vkCurrLayout, &clearValue, 1, &range);
}

void BaseVulkanTexture::vkCopyFromImage(VkCommandBuffer cmdBuffer, VkImage srcImage) {
    auto* dispatch = VRManager::instance().VK->GetDeviceDispatch();

    VkImageAspectFlags aspectMask = GetAspectMask();
    const VkImageCopy region = {
        .srcSubresource = { aspectMask, 0, 0, 1 },
        .srcOffset = { 0, 0, 0 },
        .dstSubresource = { aspectMask, 0, 0, 1 },
        .dstOffset = { 0, 0, 0 },
        .extent = {
            .width = m_width,
            .height = m_height,
            .depth = 1
        }
    };

    VulkanUtils::DebugPipelineBarrier(cmdBuffer);
    dispatch->CmdCopyImage(cmdBuffer, srcImage, VK_IMAGE_LAYOUT_GENERAL, m_vkImage, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    VulkanUtils::DebugPipelineBarrier(cmdBuffer);
}

void BaseVulkanTexture::vkUpload(VkCommandBuffer cmdBuffer, const void* data, size_t size) {
    m_uploadCommandBuffer = cmdBuffer;
    isStagingUpload = true;

    auto* dispatch = VRManager::instance().VK->GetDeviceDispatch();
    VkDevice device = VRManager::instance().VK->GetDevice();

    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.queueFamilyIndexCount = 0;
    bufferInfo.pQueueFamilyIndices = nullptr;

    checkVkResult(dispatch->CreateBuffer(device, &bufferInfo, nullptr, &m_stagingBuffer), "Failed to create staging buffer!");

    VkMemoryRequirements memRequirements;
    dispatch->GetBufferMemoryRequirements(device, m_stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = VRManager::instance().VK->FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    checkVkResult(dispatch->AllocateMemory(device, &allocInfo, nullptr, &m_stagingMemory), "Failed to allocate staging buffer memory!");

    checkVkResult(dispatch->BindBufferMemory(device, m_stagingBuffer, m_stagingMemory, 0), "Failed to bind staging buffer memory!");

    void* mappedData;
    checkVkResult(dispatch->MapMemory(device, m_stagingMemory, 0, size, 0, &mappedData), "Failed to map staging buffer memory!");
    memcpy(mappedData, data, size);
    dispatch->UnmapMemory(device, m_stagingMemory);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = GetAspectMask();
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { .x = 0, .y = 0, .z = 0 };
    region.imageExtent = {
        .width = m_width,
        .height = m_height,
        .depth = 1
    };

    VulkanUtils::DebugPipelineBarrier(cmdBuffer);
    dispatch->CmdCopyBufferToImage(cmdBuffer, m_stagingBuffer, m_vkImage, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    VulkanUtils::DebugPipelineBarrier(cmdBuffer);
}

// this relies on Cemu always only having one command buffer in flight
void BaseVulkanTexture::vkTryToFinishAnyUploads(VkCommandBuffer cmdBuffer) {
    if (isStagingUpload && m_uploadCommandBuffer != cmdBuffer) {
        isStagingUpload = false;
        m_uploadCommandBuffer = VK_NULL_HANDLE;

        auto* dispatch = VRManager::instance().VK->GetDeviceDispatch();
        VkDevice device = VRManager::instance().VK->GetDevice();
        if (m_stagingBuffer != VK_NULL_HANDLE) {
            dispatch->DestroyBuffer(device, m_stagingBuffer, nullptr);
            m_stagingBuffer = VK_NULL_HANDLE;
        }
        if (m_stagingMemory != VK_NULL_HANDLE) {
            dispatch->FreeMemory(device, m_stagingMemory, nullptr);
            m_stagingMemory = VK_NULL_HANDLE;
        }
    }
}

VulkanTexture::VulkanTexture(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, bool disableAlphaThroughSwizzling): BaseVulkanTexture(width, height, format) {
    const auto* dispatch = VRManager::instance().VK->GetDeviceDispatch();

    VkImageCreateInfo imageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageCreateInfo.flags = 0;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = format;
    imageCreateInfo.extent = {
        .width = m_width,
        .height = m_height,
        .depth = 1
    };
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = usage;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.queueFamilyIndexCount = 0;
    imageCreateInfo.pQueueFamilyIndices = nullptr;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    checkVkResult(dispatch->CreateImage(VRManager::instance().VK->GetDevice(), &imageCreateInfo, nullptr, &m_vkImage), "Failed to create image!");

    VkMemoryRequirements memRequirements;
    dispatch->GetImageMemoryRequirements(VRManager::instance().VK->GetDevice(), m_vkImage, &memRequirements);

    uint32_t memoryTypeIndex = VRManager::instance().VK->FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    checkVkResult(dispatch->AllocateMemory(VRManager::instance().VK->GetDevice(), &allocInfo, nullptr, &m_vkMemory), "Failed to allocate memory!");

    checkVkResult(dispatch->BindImageMemory(VRManager::instance().VK->GetDevice(), m_vkImage, m_vkMemory, 0), "Failed to bind memory to image!");

    VkImageViewCreateInfo imageViewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    imageViewCreateInfo.image = m_vkImage;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = format;
    imageViewCreateInfo.components = {
        .r = VK_COMPONENT_SWIZZLE_IDENTITY,
        .g = VK_COMPONENT_SWIZZLE_IDENTITY,
        .b = VK_COMPONENT_SWIZZLE_IDENTITY,
        .a = disableAlphaThroughSwizzling ? VK_COMPONENT_SWIZZLE_ONE : VK_COMPONENT_SWIZZLE_IDENTITY
    };
    imageViewCreateInfo.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
    };
    checkVkResult(dispatch->CreateImageView(VRManager::instance().VK->GetDevice(), &imageViewCreateInfo, nullptr, &m_vkImageView), "Failed to create image view!");
}

VulkanTexture::~VulkanTexture() {
    if (m_vkImageView != VK_NULL_HANDLE) {
        VRManager::instance().VK->GetDeviceDispatch()->DestroyImageView(VRManager::instance().VK->GetDevice(), m_vkImageView, nullptr);
        m_vkImageView = VK_NULL_HANDLE;
    }
}

VulkanFramebuffer::VulkanFramebuffer(uint32_t width, uint32_t height, VkFormat format, VkRenderPass renderPass): VulkanTexture(width, height, format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
    const auto* dispatch = VRManager::instance().VK->GetDeviceDispatch();

    VkFramebufferCreateInfo framebufferInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &m_vkImageView;
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;
    checkVkResult(dispatch->CreateFramebuffer(dispatch->Device, &framebufferInfo, nullptr, &m_framebuffer), "Failed to create framebuffer!");
}

VulkanFramebuffer::~VulkanFramebuffer() {
    if (m_framebuffer != VK_NULL_HANDLE)
        VRManager::instance().VK->GetDeviceDispatch()->DestroyFramebuffer(VRManager::instance().VK->GetDevice(), m_framebuffer, nullptr);
}

// ===========================================================================
// SecondDeviceTexture: the Device B (second, unhooked Vulkan device) side.
// Was the D3D12 `Texture` class; now a plain Vulkan resource on a second
// VkDevice, imported from Device A's (the hooked device's) FD export.
// ===========================================================================

SecondDeviceTexture::SecondDeviceTexture(uint32_t width, uint32_t height, VkFormat format): m_secondDeviceFormat(format) {
    // Actual creation happens in SharedTexture's constructor, which needs to chain
    // the external-memory import info onto the same vkCreateImage/vkAllocateMemory
    // calls Device A's export produced - there's no meaningful "just create a plain
    // texture" case for this class the way the old D3D12 Texture base had (that one
    // always immediately created its own committed resource + shared handle; here
    // the import needs Device A's fd to already exist, so SharedTexture does it all
    // itself, same structure as the original code).
}

void SecondDeviceTexture::vkSignalSemaphore(uint64_t value) {
    static uint32_t s_signalCount = 0;
    s_signalCount++;
    if (s_signalCount % 500 == 0) {
        Log::print<INTEROP>("Device B Signal #{}: texture={}, signaling to {}", s_signalCount, (void*)this, value);
    }

    SetLastSignalledValue(value);

    uint64_t signalValue = value;
    VkTimelineSemaphoreSubmitInfo timelineInfo = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &signalValue;

    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.pNext = &timelineInfo;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &m_secondDeviceSemaphore;
    checkVkResult(VRManager::instance().VK2->fn().vkQueueSubmit(VRManager::instance().VK2->GetQueue(), 1, &submit, VK_NULL_HANDLE), "Device B semaphore signal FAILED!");
}

void SecondDeviceTexture::vkWaitForSemaphore(uint64_t value) {
    static uint32_t s_waitCount = 0;
    s_waitCount++;
    if (s_waitCount % 500 == 0) {
        Log::print<INTEROP>("Device B Wait #{}: texture={}, waiting for {}", s_waitCount, (void*)this, value);
    }

    SetLastAwaitedValue(value);

    VkSemaphoreWaitInfo waitInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &m_secondDeviceSemaphore;
    waitInfo.pValues = &value;
    checkVkResult(VRManager::instance().VK2->fn().vkWaitSemaphores(VRManager::instance().VK2->GetDevice(), &waitInfo, UINT64_MAX), "Device B semaphore wait FAILED!");
}

SecondDeviceTexture::~SecondDeviceTexture() {
    VkDevice device = VRManager::instance().VK2->GetDevice();
    if (m_secondDeviceImageView != VK_NULL_HANDLE) {
        VRManager::instance().VK2->fn().vkDestroyImageView(device, m_secondDeviceImageView, nullptr);
    }
    if (m_secondDeviceImage != VK_NULL_HANDLE) {
        VRManager::instance().VK2->fn().vkDestroyImage(device, m_secondDeviceImage, nullptr);
    }
    if (m_secondDeviceMemory != VK_NULL_HANDLE) {
        VRManager::instance().VK2->fn().vkFreeMemory(device, m_secondDeviceMemory, nullptr);
    }
    if (m_secondDeviceSemaphore != VK_NULL_HANDLE) {
        VRManager::instance().VK2->fn().vkDestroySemaphore(device, m_secondDeviceSemaphore, nullptr);
    }
}

void SecondDeviceTexture::vkTransitionLayout(VkCommandBuffer cmdList, VkImageLayout newLayout) {
    if (m_secondDeviceCurrState == newLayout) {
        return;
    }

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = m_secondDeviceCurrState;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_secondDeviceImage;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    m_secondDeviceCurrState = newLayout;
    VRManager::instance().VK2->fn().vkCmdPipelineBarrier(cmdList, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// ===========================================================================
// SharedTexture: Device A (hooked Vulkan device, BaseVulkanTexture) creates
// and EXPORTS the image + a timeline semaphore via opaque FDs; Device B
// (second Vulkan device, SecondDeviceTexture) IMPORTS them. This is the
// opposite allocation direction from the original D3D12 version (there,
// D3D12 allocated and Vulkan imported) - doesn't matter which side owns the
// allocation, only that both sides see the same memory, and this direction
// is the one already validated end-to-end against the real headset.
// ===========================================================================

SharedTexture::SharedTexture(uint32_t width, uint32_t height, VkFormat vkFormat): SecondDeviceTexture(width, height, vkFormat), BaseVulkanTexture(width, height, vkFormat) {
    const auto* dispatch = VRManager::instance().VK->GetDeviceDispatch();
    VkDevice deviceA = VRManager::instance().VK->GetDevice();
    VkPhysicalDevice physicalDeviceA = VRManager::instance().VK->GetPhysicalDevice();

    // ---- Device A: create the image with export capability ----
    VkExternalMemoryImageCreateInfo externalMemoryImageCreateInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
    externalMemoryImageCreateInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkImageCreateInfo imageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageCreateInfo.pNext = &externalMemoryImageCreateInfo;
    imageCreateInfo.flags = 0;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = vkFormat;
    imageCreateInfo.extent = {
        .width = m_width,
        .height = m_height,
        .depth = 1
    };
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = (VulkanUtils::IsDepthFormat(vkFormat) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.queueFamilyIndexCount = 0;
    imageCreateInfo.pQueueFamilyIndices = nullptr;
    imageCreateInfo.initialLayout = m_vkCurrLayout;
    checkVkResult(dispatch->CreateImage(deviceA, &imageCreateInfo, nullptr, &m_vkImage), "Failed to create image for shared texture!");

    VkMemoryRequirements memRequirements;
    dispatch->GetImageMemoryRequirements(deviceA, m_vkImage, &memRequirements);

    VkExportMemoryAllocateInfo exportMemoryAllocInfo = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
    exportMemoryAllocInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.pNext = &exportMemoryAllocInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = VRManager::instance().VK->FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    checkVkResult(dispatch->AllocateMemory(deviceA, &allocInfo, nullptr, &m_vkMemory), "Failed to allocate memory for shared texture!");
    checkVkResult(dispatch->BindImageMemory(deviceA, m_vkImage, m_vkMemory, 0), "Failed to bind memory to image for shared texture!");

    // Exportable timeline semaphore on Device A
    VkExportSemaphoreCreateInfo exportSemInfo = { VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO };
    exportSemInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkSemaphoreTypeCreateInfo timelineCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    timelineCreateInfo.pNext = &exportSemInfo;
    timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineCreateInfo.initialValue = 0;
    VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    semaphoreCreateInfo.pNext = &timelineCreateInfo;
    checkVkResult(dispatch->CreateSemaphore(deviceA, &semaphoreCreateInfo, nullptr, &m_vkSemaphore), "Failed to create timeline semaphore for shared texture!");

    // ---- Export FDs from Device A ----
    auto vkGetMemoryFdKHR_ = (PFN_vkGetMemoryFdKHR)dispatch->GetDeviceProcAddr(deviceA, "vkGetMemoryFdKHR");
    auto vkGetSemaphoreFdKHR_ = (PFN_vkGetSemaphoreFdKHR)dispatch->GetDeviceProcAddr(deviceA, "vkGetSemaphoreFdKHR");
    checkAssert(vkGetMemoryFdKHR_ != nullptr && vkGetSemaphoreFdKHR_ != nullptr, "Failed to load vkGetMemoryFdKHR/vkGetSemaphoreFdKHR on Device A!");

    VkMemoryGetFdInfoKHR memFdInfo = { VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR };
    memFdInfo.memory = m_vkMemory;
    memFdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    int memoryFd = -1;
    checkVkResult(vkGetMemoryFdKHR_(deviceA, &memFdInfo, &memoryFd), "Failed to export shared texture memory fd from Device A!");

    VkSemaphoreGetFdInfoKHR semFdInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR };
    semFdInfo.semaphore = m_vkSemaphore;
    semFdInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    int semaphoreFd = -1;
    checkVkResult(vkGetSemaphoreFdKHR_(deviceA, &semFdInfo, &semaphoreFd), "Failed to export shared texture semaphore fd from Device A!");

    // ---- Device B: import both fds ----
    VkDevice deviceB = VRManager::instance().VK2->GetDevice();
    VkPhysicalDevice physicalDeviceB = VRManager::instance().VK2->GetPhysicalDevice();

    VkExternalMemoryImageCreateInfo importImageInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
    importImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkImageCreateInfo importCreateInfo = imageCreateInfo;
    importCreateInfo.pNext = &importImageInfo;
    importCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    checkVkResult(VRManager::instance().VK2->fn().vkCreateImage(deviceB, &importCreateInfo, nullptr, &m_secondDeviceImage), "Failed to create image on Device B for shared texture import!");

    VkMemoryRequirements importMemReqs;
    VRManager::instance().VK2->fn().vkGetImageMemoryRequirements(deviceB, m_secondDeviceImage, &importMemReqs);

    VkImportMemoryFdInfoKHR importMemInfo = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR };
    importMemInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    importMemInfo.fd = memoryFd; // ownership transferred to Vulkan on success

    VkMemoryDedicatedAllocateInfo dedicatedInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    dedicatedInfo.pNext = &importMemInfo;
    dedicatedInfo.image = m_secondDeviceImage;

    VkMemoryAllocateInfo importAllocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    importAllocInfo.pNext = &dedicatedInfo;
    importAllocInfo.allocationSize = importMemReqs.size;
    importAllocInfo.memoryTypeIndex = VRManager::instance().VK2->FindMemoryType(importMemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    checkVkResult(VRManager::instance().VK2->fn().vkAllocateMemory(deviceB, &importAllocInfo, nullptr, &m_secondDeviceMemory), "Failed to import shared texture memory on Device B!");
    checkVkResult(VRManager::instance().VK2->fn().vkBindImageMemory(deviceB, m_secondDeviceImage, m_secondDeviceMemory, 0), "Failed to bind imported memory to image on Device B!");

    VkImageViewCreateInfo secondDeviceViewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    secondDeviceViewInfo.image = m_secondDeviceImage;
    secondDeviceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    secondDeviceViewInfo.format = vkFormat;
    secondDeviceViewInfo.subresourceRange = {
        .aspectMask = (VkImageAspectFlags)(VulkanUtils::IsDepthFormat(vkFormat) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT),
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
    };
    checkVkResult(VRManager::instance().VK2->fn().vkCreateImageView(deviceB, &secondDeviceViewInfo, nullptr, &m_secondDeviceImageView), "Failed to create Device B image view for shared texture!");

    VkSemaphoreTypeCreateInfo importSemTypeInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    importSemTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    importSemTypeInfo.initialValue = 0;
    VkSemaphoreCreateInfo importSemCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    importSemCreateInfo.pNext = &importSemTypeInfo;
    checkVkResult(VRManager::instance().VK2->fn().vkCreateSemaphore(deviceB, &importSemCreateInfo, nullptr, &m_secondDeviceSemaphore), "Failed to create semaphore on Device B for shared texture import!");

    auto vkImportSemaphoreFdKHR_ = (PFN_vkImportSemaphoreFdKHR)VRManager::instance().VK2->fn().vkGetDeviceProcAddr(deviceB, "vkImportSemaphoreFdKHR");
    checkAssert(vkImportSemaphoreFdKHR_ != nullptr, "Failed to load vkImportSemaphoreFdKHR on Device B!");

    VkImportSemaphoreFdInfoKHR importSemFdInfo = { VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR };
    importSemFdInfo.semaphore = m_secondDeviceSemaphore;
    importSemFdInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    importSemFdInfo.fd = semaphoreFd; // ownership transferred to Vulkan on success
    checkVkResult(vkImportSemaphoreFdKHR_(deviceB, &importSemFdInfo), "Failed to import shared texture semaphore fd on Device B!");
}

SharedTexture::~SharedTexture() {
    if (m_vkSemaphore != VK_NULL_HANDLE)
        VRManager::instance().VK->GetDeviceDispatch()->DestroySemaphore(VRManager::instance().VK->GetDevice(), m_vkSemaphore, nullptr);
}

void SharedTexture::Init(const VkCommandBuffer& cmdBuffer) {
    // transition to GENERAL for interop usage on both sides
    VulkanUtils::TransitionLayout(cmdBuffer, m_vkImage, m_vkCurrLayout, VK_IMAGE_LAYOUT_GENERAL, GetAspectMask());
    VulkanUtils::DebugPipelineBarrier(cmdBuffer);

    {
        RND_Vulkan2::CommandContext<true> transitionInitialTextures(VRManager::instance().VK2.get(), [this](RND_Vulkan2::CommandContext<true>* context) {
            this->SecondDeviceTexture::vkTransitionLayout(context->GetRecordList(), VK_IMAGE_LAYOUT_GENERAL);
        });
    }
}


void SharedTexture::CopyFromVkImage(VkCommandBuffer cmdBuffer, VkImage srcImage) {
    static uint32_t s_copyCount = 0;
    s_copyCount++;

    auto* dispatch = VRManager::instance().VK->GetDeviceDispatch();

    if (srcImage == VK_NULL_HANDLE) {
        Log::print<ERROR>("CopyFromVkImage #{}: srcImage is NULL!", s_copyCount);
        return;
    }
    if (this->m_vkImage == VK_NULL_HANDLE) {
        Log::print<ERROR>("CopyFromVkImage #{}: destination m_vkImage is NULL!", s_copyCount);
        return;
    }

    if (s_copyCount % 500 == 0) {
        Log::print<INTEROP>("CopyFromVkImage #{}: src={}, dst={}, size={}x{}", s_copyCount, (void*)srcImage, (void*)this->m_vkImage, m_width, m_height);
    }

    VkImageAspectFlags aspectMask = GetAspectMask();
    VkImageCopy copyRegion = {
        .srcSubresource = { aspectMask, 0, 0, 1 },
        .srcOffset = { 0, 0, 0 },
        .dstSubresource = { aspectMask, 0, 0, 1 },
        .dstOffset = { 0, 0, 0 },
        .extent = { m_width, m_height, 1 }
    };

    // Copy using the correct layouts
    VulkanUtils::DebugPipelineBarrier(cmdBuffer);
    dispatch->CmdCopyImage(cmdBuffer, srcImage, VK_IMAGE_LAYOUT_GENERAL, this->m_vkImage, VK_IMAGE_LAYOUT_GENERAL, 1, &copyRegion);
    VulkanUtils::DebugPipelineBarrier(cmdBuffer);
}
