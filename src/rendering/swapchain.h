#pragma once

template <VkFormat T>
class Swapchain {
public:
    Swapchain(uint32_t width, uint32_t height, uint32_t sampleCount);
    ~Swapchain();

    void PrepareRendering();
    VkImage StartRendering();
    void FinishRendering();

    XrSwapchain GetHandle() const { return m_swapchain; };
    VkImage GetTexture() const { return m_swapchainTextures[m_swapchainImageIdx]; };
    VkImageView GetTextureView() const { return m_swapchainViews[m_swapchainImageIdx]; };

    VkFormat GetFormat() const { return m_format; };
    [[nodiscard]] uint32_t GetWidth() const { return m_width; };
    [[nodiscard]] uint32_t GetHeight() const { return m_height; };

private:
    XrSwapchain m_swapchain = XR_NULL_HANDLE;
    uint32_t m_width;
    uint32_t m_height;
    VkFormat m_format;

    std::vector<VkImage> m_swapchainTextures;
    std::vector<VkImageView> m_swapchainViews;
    uint32_t m_swapchainImageIdx = 0;
};
