#pragma once

#include <cstdint>
#include <memory>
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
struct Image
{
public:
  uint32_t width;
  uint32_t height;
  ImageFormat format;
  void* native_handle = nullptr;
  void* descriptor = nullptr;
};

}  // namespace core
