#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <nvvk/resource_allocator.hpp>

#include "core/Image.hpp"

namespace core
{

class VulkanImage : public Image
{
public:
  VulkanImage(nvvk::ResourceAllocator& alloc, const ImageCreateInfo& info) :
      m_allocator(&alloc), m_info(info)
  {
    // 1. Map agnostic format to Vulkan format
    VkFormat vkFormat = convertFormat(info.format);

    // 2. Create Vulkan Image using nvvk helpers
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (info.usage == ImageUsage::Attachment)
      usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    nvvk::Image image;
    VkImageCreateInfo imageInfo{};
    imageInfo.usage = usage;
    imageInfo.format = vkFormat;
    imageInfo.extent = {info.width, info.height};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;

    VkResult result = m_allocator->createImage(m_image, imageInfo);
    if (result != VK_SUCCESS)
    {
      LOGE("Failed to create VkImage (VkResult = %d)", result);
      throw std::runtime_error("VkImage creation failed");
    }
  }

  ~VulkanImage() { m_allocator->destroyImage(m_image); }

  void* getNativeHandle() const override { return (void*) m_image.image; }
  void* getDescriptorSet() const override { return (void*) m_descriptorSet; }

  uint32_t getWidth() const override { return m_info.width; }
  uint32_t getHeight() const override { return m_info.height; }
  ImageFormat getFormat() const override { return m_info.format; }

private:
  nvvk::ResourceAllocator* m_allocator;
  nvvk::Image m_image;
  void* m_descriptorSet{nullptr};
  ImageCreateInfo m_info;

  VkFormat convertFormat(ImageFormat f)
  {
    switch (f)
    {
      case ImageFormat::RGBA32_SFLOAT:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
      default:
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
  }
};

}  // namespace core