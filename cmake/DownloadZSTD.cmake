# ==========================================
# Zstd 1.5.7 Setup
# ==========================================
include(FetchContent)

# 1. Configure Zstd Options
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
# 'BUILD_TESTING' is the magic variable that stops standard CTest targets
set(BUILD_TESTING OFF CACHE BOOL "" FORCE) 
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)

# 2. Check if Zstd is already downloaded
set(ZSTD_INSTALL_DIR "${CMAKE_SOURCE_DIR}/ext/zstd")
set(ZSTD_CMAKE_FILE "${ZSTD_INSTALL_DIR}/build/cmake/CMakeLists.txt")

if(EXISTS "${ZSTD_CMAKE_FILE}")
    message(STATUS "Found Zstd in ${ZSTD_INSTALL_DIR} (Skipping Download)")
    
    # CASE A: It exists. Declare it WITHOUT a URL. 
    # This treats it as a local folder and skips all download logic.
    FetchContent_Declare(
        zstd
        SOURCE_DIR "${ZSTD_INSTALL_DIR}"
        SOURCE_SUBDIR build/cmake
    )
else()
    message(STATUS "Downloading Zstd v1.5.7 to ${ZSTD_INSTALL_DIR}...")
    
    # CASE B: It's missing. Declare it WITH a URL.
    FetchContent_Declare(
        zstd
        URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
        SOURCE_DIR "${ZSTD_INSTALL_DIR}"
        SOURCE_SUBDIR build/cmake
    )
endif()

# 3. Build (Add subdirectory)
FetchContent_MakeAvailable(zstd)

# 4. Link alias
if(TARGET libzstd_static)
    add_library(Zstd::Zstd ALIAS libzstd_static)
endif()


