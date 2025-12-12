# Default preset if not provided:
#   make build preset=release
#   make build preset=debug
preset ?= release
BUILD_DIR := build

.PHONY: configure build install clean help

configure:
	cmake --preset $(preset) -B $(BUILD_DIR)

build:
	cmake --build $(BUILD_DIR) --preset $(preset) --parallel

install:
	cmake --build $(BUILD_DIR) --parallel --target install

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Available targets:"
	@echo "  make configure preset=<preset>   - Run CMake configure step (default: release)"
	@echo "  make build preset=<preset>        - Build using CMake with the given preset"
	@echo "  make install                      - Run 'cmake --build --target install'"
	@echo "  make clean                        - Delete the build directory"
	@echo ""
	@echo "Examples:"
	@echo "  make configure preset=debug"
	@echo "  make build preset=release"
	@echo "  make install"
