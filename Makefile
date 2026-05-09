# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------

# Default preset if not provided
preset ?= release
BUILD_DIR := build

# Preset → CMake config mapping
MAP_release        := Release
MAP_debug          := Debug
MAP_relwithdebinfo := RelWithDebInfo
MAP_dev            := RelWithDebInfo

# Resolve config type (fallback to Release)
CONFIG_TYPE := $(or $(MAP_$(preset)),Release)

# Derived paths
BIN_DIR := $(BUILD_DIR)/bin/$(CONFIG_TYPE)
CACHE_DIR := .cache/models

.PHONY: configure build rebuild install clean test help

configure:
	cmake --preset $(preset) -B $(BUILD_DIR) 

build:
	cmake --build $(BUILD_DIR) --preset $(preset) --config $(CONFIG_TYPE) --parallel

rebuild:
	cmake --build $(BUILD_DIR) --preset $(preset) --clean-first --parallel

install:
	cmake --build $(BUILD_DIR) --parallel --target install

clean:
	rm -rf $(BUILD_DIR)
	ccache --clear && ccache -z
	$(MAKE) clear_cache

clear_cache:
	rm -rf ${CACHE_DIR}

# ------------------------------------------------------------------------------
# Unit tests (standalone build – no Vulkan/GLFW/Slang required)
# Uses tests/CMakePresets.json; cd into tests/ so --preset is resolved there.
# ------------------------------------------------------------------------------

test:
	cd tests && cmake --preset tests-debug
	cd tests && cmake --build --preset tests-debug
	cd tests && ctest --preset tests-debug

# ------------------------------------------------------------------------------
# Run helpers
# ------------------------------------------------------------------------------

args ?=
run_%: build
	@set -e; \
	exe="$(BIN_DIR)/$*"; \
	if [ -x "$$exe" ]; then \
		echo "Executing: $$exe $(args)"; \
		"$$exe" $(args); \
	else \
		echo "Error: Executable '$$exe' not found or not executable"; \
		exit 1; \
	fi


help:
	@echo "Available targets:"
	@echo "  make configure preset=<preset>        - Run CMake configure step (default: release)"
	@echo "  make build preset=<preset>             - Build using CMake with the given preset"
	@echo "  make install                           - Run 'cmake --build --target install'"
	@echo "  make clean                             - Delete the build directory"
	@echo "  make test                              - Configure, build and run unit tests (tests/CMakePresets.json)"
	@echo "  make run_<target> [args=\"...\"]        - Run a specific executable from \$(BIN_DIR)"
	@echo ""
	@echo "Examples:"
	@echo "  make configure preset=debug"
	@echo "  make build preset=release"
	@echo "  make install"
	@echo "  make test"
	@echo "  make run_my_app args=\"--input file.txt --verbose\""
