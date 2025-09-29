# Top-level convenience Makefile (no hardcoded build/<cfg>)
# Usage examples:
#   make configure cfg=release B=build/release
#   make build     cfg=release B=build/release t=viewer
#   make run       cfg=release B=build/release t=viewer args="--help"

cfg ?= release
B ?= build
# override with: B=build/release


# Map cfg -> generator's config dir name
ifeq ($(cfg),debug)
  CONFIG_DIR := Debug
else ifeq ($(cfg),release)
  CONFIG_DIR := Release
else ifeq ($(cfg),relwithdebinfo)
  CONFIG_DIR := RelWithDebInfo
else
  CONFIG_DIR := $(cfg)
endif

# Auto-configure helper: runs cmake if CMakeCache.txt is missing
define ensure_configured
	@if [ ! -f "$(B)/CMakeCache.txt" ]; then \
		echo ">> configuring preset '$(cfg)' into '$(B)'"; \
		cmake --preset $(cfg) -B $(B); \
	fi
endef

.PHONY: help configure build rebuild test install package run list viewer plugins tidy fmt clean distclean

help:
	@echo "Available targets:"
	@echo ""
	@echo "  configure   [cfg=debug|release] [B=build/<dir>]          # configure with a preset"
	@echo "  build       [cfg=...] [B=...] [t=<cmake-target>]          # build (optionally one target)"
	@echo "  rebuild     [cfg=...] [B=...] [t=<cmake-target>]          # clean & build target(s)"
	@echo ""
	@echo "  test        [cfg=...] [B=...]                             # run ctest"
	@echo "  install     [cfg=...] [B=...]                             # install into prefix/DESTDIR"
	@echo "  package     [cfg=...] [B=...]                             # create CPack package"
	@echo ""
	@echo "  run         t=<app> [args='--foo'] [cfg=...] [B=...]      # build & run an app"
	@echo "  list        [cfg=...] [B=...]                             # list available CMake/Ninja targets"
	@echo ""
	@echo "  clean       [cfg=...] [B=...]                             # remove one config build dir"
	@echo "  distclean                                                  # remove all build dirs"
	@echo ""
	@echo "Shortcuts:"
	@echo "  fmt         run clang-format (if target exists)"
	@echo "  tidy        run clang-tidy (if target exists)"



# remove one config subfolder (e.g. build/bin_x64/Release)
clean:
	@target="$(B)/bin_x64/$(cfg)"; \
	if [ -d "$$target" ]; then \
	  echo ">> removing $$target"; \
	  rm -rf "$$target"; \
	else \
	  echo ">> nothing to clean (no $$target)"; \
	fi

# remove the whole build tree
distclean:
	@if [ -d "$(B)" ]; then \
	  echo ">> removing $(B) (all configs)"; \
	  rm -rf "$(B)"; \
	else \
	  echo ">> nothing to distclean (no $(B))"; \
	fi


configure:
	cmake --preset $(cfg) -B $(B)

build:
	$(call ensure_configured)
	@if [ -n "$(t)" ]; then cmake --build $(B) --target $(t); \
	else cmake --build $(B); fi

rebuild:
	$(call ensure_configured)
	@if [ -n "$(t)" ]; then cmake --build $(B) --target $(t) --clean-first; \
	else cmake --build $(B) --clean-first; fi

test:
	$(call ensure_configured)
	ctest --test-dir $(B) --output-on-failure

install:
	$(call ensure_configured)
	cmake --install $(B)

package:
	$(call ensure_configured)
	cmake --build $(B) --target package

# list all ninja/cmake targets in the current build
list:
	$(call ensure_configured)
	@{ command -v ninja >/dev/null 2>&1 && ninja -C $(B) -t targets all || true; } \
	 || { echo "No ninja in PATH or $(B) missing."; }

# run clang-format (requires a custom target in your CMake)
fmt:
	$(call ensure_configured)
	$(MAKE) build cfg=$(cfg) B=$(B) t=clang-format

# run clang-tidy (requires a custom target in your CMake)
tidy:
	$(call ensure_configured)
	$(MAKE) build cfg=$(cfg) B=$(B) t=clang-tidy

# Convenience shortcuts
HelloWorld_app:  ; $(MAKE) run cfg=$(cfg) B=$(B) t=HelloWorld_app


# Map cfg -> config dir name
ifeq ($(cfg),debug)
  CONFIG_DIR := Debug
else ifeq ($(cfg),release)
  CONFIG_DIR := Release
else ifeq ($(cfg),relwithdebinfo)
  CONFIG_DIR := RelWithDebInfo
else
  CONFIG_DIR := $(cfg)
endif

run:
	@$(call ensure_configured)
	@$(MAKE) build cfg=$(cfg) B=$(B) t=$(t)
	@if [ -z "$(t)" ]; then \
	  echo "Set t=<app target>, e.g. 'make run t=HelloWorld_app'"; \
	  exit 2; \
	fi; \
	# candidate locations
	candidates="$(B)/$(t) $(B)/bin/$(t) $(B)/bin/$(CONFIG_DIR)/$(t) $(B)/bin_x64/$(CONFIG_DIR)/$(t)"; \
	exe_found=""; \
	for p in $$candidates; do \
	  if [ -x "$$p" ]; then exe_found="$$p"; break; fi; \
	done; \
	if [ -z "$$exe_found" ]; then \
	  exe_found=$$(find "$(B)" -maxdepth 6 -type f -perm -111 -name "$(t)" | head -n1); \
	fi; \
	if [ -z "$$exe_found" ]; then \
	  echo "Could not locate executable for '$(t)' under $(B)"; \
	  exit 1; \
	fi; \
	echo ">> running $$exe_found $(args)"; \
	"$$exe_found" $(args)
