# ==========================================
# zlib-ng 2.2.3 Setup
# ==========================================
include(FetchContent)

# 1. Configuration
# Performance & Safety
set(ZLIB_COMPAT OFF CACHE BOOL "" FORCE)      # Use 'zng_' prefix to avoid conflicts with system zlib
set(ZLIB_ENABLE_SIMD ON CACHE BOOL "" FORCE)  # Enable SSE/AVX/NEON optimizations
set(WITH_NATIVE_INSTRUCTIONS ON CACHE BOOL "" FORCE)

# Disable Bloat (Tests & Examples)
set(ZLIB_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ZLIBNG_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_GTEST OFF CACHE BOOL "" FORCE)

# Disable Installation
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_LIBRARIES ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_HEADERS ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_FILES ON CACHE BOOL "" FORCE)

# 2. Check if zlib-ng is already downloaded
set(ZLIBNG_INSTALL_DIR "${CMAKE_SOURCE_DIR}/ext/zlib-ng")
set(ZLIBNG_CMAKE_FILE "${ZLIBNG_INSTALL_DIR}/CMakeLists.txt")

if(EXISTS "${ZLIBNG_CMAKE_FILE}")
    message(STATUS "Found zlib-ng in ${ZLIBNG_INSTALL_DIR} (Skipping Download)")
    
    # CASE A: It exists. Declare it WITHOUT a URL.
    FetchContent_Declare(
        zlib_ng
        SOURCE_DIR "${ZLIBNG_INSTALL_DIR}"
    )
else()
    message(STATUS "Downloading zlib-ng v2.2.3 to ${ZLIBNG_INSTALL_DIR}...")
    
    # CASE B: It's missing. Declare it WITH a URL.
    FetchContent_Declare(
        zlib_ng
        URL https://github.com/zlib-ng/zlib-ng/archive/refs/tags/2.2.3.tar.gz
        SOURCE_DIR "${ZLIBNG_INSTALL_DIR}"
    )
endif()

# 3. Build
FetchContent_MakeAvailable(zlib_ng)

# 4. Alias (Optional)
if(TARGET zlib)
    add_library(Zlib::Zlib ALIAS zlib)
endif()
