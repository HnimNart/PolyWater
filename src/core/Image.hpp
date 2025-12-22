#pragma once

#include <filesystem>
#include <string>

namespace core
{

enum class ImageFormat
{
  RGBA8_UNORM,
  RGBA32_SFLOAT,
  DEPTH32_SFLOAT,
  // Add more as needed
};

enum class ImageUsage
{
  Texture,
  Attachment,
  Storage
};

struct ImageCreateInfo
{
  uint32_t width{0};
  uint32_t height{0};
  ImageFormat format{ImageFormat::RGBA8_UNORM};
  ImageUsage usage{ImageUsage::Texture};
  std::string debugName;
};

/**
 * @brief Agnostic Image interface.
 * The actual Vulkan/DX12 resources are managed by the backend.
 */
class Image
{
public:
  virtual ~Image() = default;

  virtual uint32_t getWidth() const = 0;
  virtual uint32_t getHeight() const = 0;
  virtual ImageFormat getFormat() const = 0;

  /** * @brief Returns the API-specific handle (VkImage, ID3D12Resource*, etc.)
   */
  virtual void* getNativeHandle() const = 0;

  /**
   * @brief Returns a handle for UI tools like ImGui to display the image.
   */
  virtual void* getDescriptorSet() const = 0;

  // Static factory: The Application/Backend will provide the implementation
  static std::unique_ptr<Image> create(const ImageCreateInfo& info);
};

}  // namespace core