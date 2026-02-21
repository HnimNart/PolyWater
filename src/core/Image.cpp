#include "Image.hpp"

#include "file_operations.hpp"
#include "logger.hpp"

namespace core {

/**********************************************************/
core::Image loadRawImage(const std::filesystem::path &filename)
/**********************************************************/
{
  core::Image img;
  img.filename = filename;

  int w, h, comp;
  const int req_comp = 4;
  std::string pathStr = core::utf8FromPath(filename);

  // 1. Check if the file is HDR (for EnvMaps) or Standard (for Textures)
  if (stbi_is_hdr(pathStr.c_str())) {
    float *data = stbi_loadf(pathStr.c_str(), &w, &h, &comp, req_comp);
    if (data) {
      img.width = static_cast<uint32_t>(w);
      img.height = static_cast<uint32_t>(h);
      img.format = core::ImageFormat::RGBA32_SFLOAT;

      size_t sizeInBytes =
          static_cast<size_t>(w) * h * req_comp * sizeof(float);
      img.pixels.resize(sizeInBytes);
      std::memcpy(img.pixels.data(), data, sizeInBytes);

      stbi_image_free(data);
    }
  } else {
    stbi_uc *data = stbi_load(pathStr.c_str(), &w, &h, &comp, req_comp);
    if (data) {
      img.width = static_cast<uint32_t>(w);
      img.height = static_cast<uint32_t>(h);
      img.format = core::ImageFormat::RGBA8_UNORM;

      size_t sizeInBytes =
          static_cast<size_t>(w) * h * req_comp * sizeof(stbi_uc);
      img.pixels.resize(sizeInBytes);
      std::memcpy(img.pixels.data(), data, sizeInBytes);

      stbi_image_free(data);
    }
  }

  if (!img.isValid()) {
    LOGE("STB failed to load image: %s", pathStr.c_str());
  } else {
    LOGD("Loaded %s", filename.string().c_str());
  }

  return img;
}

/**********************************************************/
void releaseImageMemory(core::Image &img)
/**********************************************************/
{
  img.pixels.clear();
  img.pixels.shrink_to_fit();
  img.width = 0;
  img.height = 0;
}

} // namespace core
