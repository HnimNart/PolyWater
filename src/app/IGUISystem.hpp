#pragma once

#include <memory>
#include <vector>

#include "AppInfo.hpp"
#include "IAppElement.hpp"
#include "core/Types.hpp"

namespace app
{

class IGUISystem
{
public:
  virtual ~IGUISystem() = default;

  // Context lifecycle
  virtual void init(const ApplicationCreateInfo& info) = 0;
  virtual void deinit() = 0;

  // Frame lifecycle
  virtual void beginFrame() = 0;
  virtual void endFrame() = 0;
  virtual void render() = 0;

  virtual bool getWindowSize(const std::string& windowName,
                             WindowSize& size) = 0;
  virtual void setWindowSize(const WindowSize& size) = 0;

  virtual void
  renderMenu(const std::vector<std::shared_ptr<IAppElement>>& elements) = 0;

  // Settings/Config
  virtual void setConfigFlags(unsigned int flags) = 0;
  virtual void loadSettings(const char* filename) = 0;
  virtual void saveSettings(const char* filename) = 0;

  // Called by the backend when the display DPI scale changes.
  // @param scaleRatio  new_scale / old_scale (multiply existing font scale by this)
  virtual void onDpiScaleChanged(float /*scaleRatio*/) {}
};

using IGUISystemPtr = std::shared_ptr<IGUISystem>;

}  // namespace app
