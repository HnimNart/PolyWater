include(FetchContent)

set(OIDN_VERSION "2.3.0")

# Define the "Permanent" location
set(OIDN_INSTALL_DIR "${CMAKE_SOURCE_DIR}/ext/oidn")

# Logic to pick the correct URL (Windows vs Linux)
if(WIN32)
    set(OIDN_PLATFORM "x86_64.windows")
    set(OIDN_EXT "zip")
else()
    set(OIDN_PLATFORM "x86_64.linux")
    set(OIDN_EXT "tar.gz")
endif()

set(OIDN_URL "https://github.com/OpenImageDenoise/oidn/releases/download/v${OIDN_VERSION}/oidn-${OIDN_VERSION}.${OIDN_PLATFORM}.${OIDN_EXT}")

# Check if we already have the specific version installed
# We look for a known file to verify the install exists
if(EXISTS "${OIDN_INSTALL_DIR}/bin/oidnDenoise" OR EXISTS "${OIDN_INSTALL_DIR}/bin/oidnDenoise.exe")
    message(STATUS "OIDN found locally in ${OIDN_INSTALL_DIR}. Skipping download.")
    set(OpenImageDenoise_ROOT "${OIDN_INSTALL_DIR}")
else()
    message(STATUS "OIDN not found or incomplete. Downloading v${OIDN_VERSION}...")
    
    FetchContent_Declare(
        oidn_binary
        URL ${OIDN_URL}
        SOURCE_DIR ${OIDN_INSTALL_DIR}
    )
    
    FetchContent_MakeAvailable(oidn_binary)
    set(OpenImageDenoise_ROOT "${OIDN_INSTALL_DIR}")
endif()

# Now find the package using the location we just ensured exists
find_package(OpenImageDenoise REQUIRED)
