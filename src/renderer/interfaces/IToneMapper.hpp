
#pragma once

#include <shaders/shared/structs.h>

#include <shaders/shared/tonemapper_io.h.slang>

class IToneMapper
{
public:
  virtual ~IToneMapper() = default;
  shaderio::TonemapperData& data()
  {
    return m_tonemapperData;
  }

protected:
  shaderio::TonemapperData m_tonemapperData{};
};
