
#pragma once

#include <shaders/shaderio.h>

#include <nvshaders_host/tonemapper.hpp>

class IPostProcessor
{
public:
  virtual ~IPostProcessor() = default;

  shaderio::TonemapperData& data() { return m_tonemapperData; }

protected:
  shaderio::TonemapperData m_tonemapperData{};
};
