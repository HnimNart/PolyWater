#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <stb/stb_image.h>

namespace core {

enum class ImageFormat { RGBA8_UNORM, RGBA32_SFLOAT, DEPTH32_SFLOAT, UNKNOWN };

enum class ImageUsage { Texture, Attachment, Storage };

struct ImageCreateInfo {
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
struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t components = 4;
  ImageFormat format = ImageFormat::UNKNOWN;

  // CPU Pixel storage
  std::vector<uint8_t> pixels;

  bool isValid() const { return !pixels.empty(); }

  // Helper to get raw float pointer for HDR maps
  float *asFloat() { return reinterpret_cast<float *>(pixels.data()); }
};

core::Image loadRawImage(const std::filesystem::path &filename);
void releaseRawImage(core::Image &image);

} // namespace core
