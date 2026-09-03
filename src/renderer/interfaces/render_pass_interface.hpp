#pragma once

#include <string_view>

#include "backend/interfaces/pass_builder.hpp"
#include "backend/interfaces/render_context_interface.hpp"

class IRenderPass
{
public:
  virtual ~IRenderPass() = default;
  virtual void init() = 0;
  virtual void setup(PassBuilder& builder) = 0;
  virtual void execute(IRenderContext& ctx) = 0;
  virtual void deinit() = 0;
  virtual std::string_view name() const = 0;
};
