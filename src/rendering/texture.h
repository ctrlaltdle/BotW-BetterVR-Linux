#pragma once

class SharedTexture;

class BaseVulkanTexture {
    friend class SharedTexture;
    friend class VulkanTexture;
public:
    BaseVulkanTexture(uint32_t width, uint32_t height, VkFormat vkFormat): m_width(width), m_height(height), m_vkFormat(vkFormat) {}
    virtual ~BaseVulkanTexture();

    void vkPipelineBarrier(VkCommandBuffer cmdBuffer);
    void vkTransitionLayout(VkCommandBuffer cmdBuffer, VkImageLayout newLayout);

    void vkClear(VkCommandBuffer cmdBuffer, VkClearColorValue color);
    void vkClearDepth(VkCommandBuffer cmdBuffer, float depth, uint32_t stencil = 0);
    void vkCopyToImage(VkCommandBuffer cmdBuffer, VkImage dstImage);
    // AMD GPU FIX: srcLayout parameter to specify the actual source image layout
    // If srcLayout is TRANSFER_SRC_OPTIMAL, assume caller has already transitioned and skip internal transitions
    void vkCopyFromImage(VkCommandBuffer cmdBuffer, VkImage srcImage);

    bool vkIsUploadingTexture() const { return isStagingUpload; }
    void vkUpload(VkCommandBuffer cmdBuffer, const void* data, size_t size);
    void vkTryToFinishAnyUploads(VkCommandBuffer cmdBuffer);

    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    VkFormat GetFormat() const { return m_vkFormat; }
    VkImageAspectFlags GetAspectMask() const;
    VkImage GetImage() const { return m_vkImage; }

protected:
    VkImage m_vkImage = VK_NULL_HANDLE;
    VkDeviceMemory m_vkMemory = VK_NULL_HANDLE;
    VkImageLayout m_vkCurrLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t m_width;
    uint32_t m_height;
    VkFormat m_vkFormat;

    // for tracking uploads and freeing staging buffers
    bool isStagingUpload = false;
    VkCommandBuffer m_uploadCommandBuffer;
    VkBuffer m_stagingBuffer;
    VkDeviceMemory m_stagingMemory;
};

class VulkanTexture : public BaseVulkanTexture {
    friend class VulkanFramebuffer;
public:
    VulkanTexture(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, bool disableAlphaThroughSwizzling);
    VulkanTexture(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage): VulkanTexture(width, height, format, usage, false) {
    }
    ~VulkanTexture() override;

    VkImageView GetImageView() const { return m_vkImageView; }

private:
    VkImageView m_vkImageView = VK_NULL_HANDLE;
};

class VulkanFramebuffer : public VulkanTexture {
public:
    VulkanFramebuffer(uint32_t width, uint32_t height, VkFormat format, VkRenderPass renderPass);
    ~VulkanFramebuffer() override;

    VkFramebuffer GetFramebuffer() const { return m_framebuffer; }
private:
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
};

// Represents the SAME underlying frame data as seen from the second, unhooked Vulkan
// device (Device B) that talks to OpenXR - imported from the hooked device's (Device
// A's) export via VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT. This replaces what
// was previously an ID3D12Resource wrapper imported from a Win32 shared handle; the
// mechanism is otherwise the same idea (Linux has no Win32 handles, so FD-based
// external memory/semaphore extensions are the equivalent), and this exact FD
// import/export path was validated standalone (see vk-vk-openxr-test/) against a
// real Meta Quest 3 through WiVRn before being wired in here.
class SecondDeviceTexture {
public:
    SecondDeviceTexture(uint32_t width, uint32_t height, VkFormat format);
    virtual ~SecondDeviceTexture();

    void vkSignalSemaphore(uint64_t value);
    void vkWaitForSemaphore(uint64_t value);
    void vkTransitionLayout(VkCommandBuffer cmdList, VkImageLayout newLayout);

    VkImage secondDeviceGetImage() const { return m_secondDeviceImage; }
    VkImageView secondDeviceGetImageView() const { return m_secondDeviceImageView; }
    VkSemaphore secondDeviceGetSemaphore() const { return m_secondDeviceSemaphore; }
    VkFormat secondDeviceGetFormat() const { return m_secondDeviceFormat; }

    uint64_t GetLastSignalledValue() const { return m_fenceLastSignaledValue; }
    uint64_t GetLastAwaitedValue() const { return m_fenceLastAwaitedValue; }

protected:
    void SetLastSignalledValue(uint64_t value) {
        static uint32_t s_signalCount = 0;
        s_signalCount++;
        if (s_signalCount % 500 == 0 || m_fenceLastSignaledValue == value) {
            Log::print<INTEROP>("Semaphore signal #{}: texture={}, value {} -> {} (last waited={})", s_signalCount, (void*)this, m_fenceLastSignaledValue, value, m_fenceLastAwaitedValue);
        }
        if (m_fenceLastSignaledValue == value && value != 0) {
            Log::print<WARNING>("Double signal detected! texture={}, value={}", (void*)this, value);
        }
        m_fenceLastSignaledValue = value;
    }
    void SetLastAwaitedValue(uint64_t value) {
        static uint32_t s_waitCount = 0;
        s_waitCount++;
        if (s_waitCount % 500 == 0 || m_fenceLastAwaitedValue == value) {
            Log::print<INTEROP>("Semaphore wait #{}: texture={}, value {} -> {} (last signaled={})", s_waitCount, (void*)this, m_fenceLastAwaitedValue, value, m_fenceLastSignaledValue);
        }
        if (m_fenceLastAwaitedValue == value && value != 0) {
            Log::print<WARNING>("Double wait detected! texture={}, value={}", (void*)this, value);
        }
        m_fenceLastAwaitedValue = value;
    }

    VkFormat m_secondDeviceFormat;
    VkImage m_secondDeviceImage = VK_NULL_HANDLE;
    VkImageView m_secondDeviceImageView = VK_NULL_HANDLE;
    VkDeviceMemory m_secondDeviceMemory = VK_NULL_HANDLE;
    VkImageLayout m_secondDeviceCurrState = VK_IMAGE_LAYOUT_UNDEFINED;
    VkSemaphore m_secondDeviceSemaphore = VK_NULL_HANDLE;

    uint64_t m_fenceLastSignaledValue = 0;
    uint64_t m_fenceLastAwaitedValue = 0;
};

class SharedTexture : public SecondDeviceTexture, public BaseVulkanTexture {
public:
    SharedTexture(uint32_t width, uint32_t height, VkFormat vkFormat);
    ~SharedTexture() override;
    void Init(const VkCommandBuffer& cmdBuffer);

    // srcImageLayout: the ACTUAL current layout of srcImage (e.g., from Cemu's CmdClearColorImage hook)
    void CopyFromVkImage(VkCommandBuffer cmdBuffer, VkImage srcImage);
    const VkSemaphore& GetSemaphore() const { return m_vkSemaphore; }

    // Timeline semaphores require strictly increasing values. Instead of ping-ponging
    // between 0 and 1, we use a monotonically increasing counter. The flow is:
    //   1. Device A (hooked) waits for value N (last Device B signal, or 0 initially)
    //   2. Device A copies, then signals N+1
    //   3. Device B waits for N+1
    //   4. Device B uses texture, then signals N+2
    //   5. Next frame: Device A waits for N+2, signals N+3, etc.

    // Get the value Device A (hooked Vulkan) should wait for (the last value Device B signaled)
    uint64_t GetVulkanWaitValue() const { return m_fenceCounter.load(); }

    // Get the value Device A should signal (increments counter)
    uint64_t GetVulkanSignalValue() { return ++m_fenceCounter; }

    // Get the value Device B (second Vulkan device) should wait for (the last value Device A signaled)
    uint64_t GetSecondDeviceWaitValue() const { return m_fenceCounter.load(); }

    // Get the value Device B should signal (increments counter)
    uint64_t GetSecondDeviceSignalValue() { return ++m_fenceCounter; }

    const VkSemaphore& GetSemaphoreForSignal(uint64_t dbg_SignalTo = 0) {
        SetLastSignalledValue(dbg_SignalTo);
        return m_vkSemaphore;
    }
    const VkSemaphore& GetSemaphoreForWait(uint64_t dbg_WaitFor = 0) {
        SetLastAwaitedValue(dbg_WaitFor);
        return m_vkSemaphore;
    }

private:
    VkSemaphore m_vkSemaphore = VK_NULL_HANDLE;
    std::atomic_bool m_activeOperation = false;
    std::atomic<uint64_t> m_fenceCounter{0};  // Monotonically increasing fence value

    // Real, live FDs handed from Device A's export to Device B's import. Owned here
    // only transiently - vkGetMemoryFdKHR/vkGetSemaphoreFdKHR each mint a fresh fd
    // referencing the same underlying resource, and the subsequent
    // vkAllocateMemory/vkImportSemaphoreFdKHR call takes ownership of it (Vulkan
    // closes it internally), matching the exact behavior validated in
    // vk-vk-openxr-test/main.cpp.
};
