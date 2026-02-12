# -------------------------------------------------------------------
# Library: core_lib (Shared)
# -------------------------------------------------------------------
set(LIB_NAME core_lib)

# 1. Collect Sources
# Using GLOB_RECURSE to pick up shape/primitives.cpp and others
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS 
    "*.cpp" 
    "*.hpp" 
    "*.h"
)

# 2. Define Shared Library
add_library(${LIB_NAME} SHARED ${CORE_SOURCES})

# 3. Essential Properties for Shared Libs
set_target_properties(${LIB_NAME} PROPERTIES
    # Ensures -fPIC is used on Linux
    POSITION_INDEPENDENT_CODE ON
    # Standard C++20
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# --- 5. Link Libraries ---
find_package(fmt REQUIRED)
find_package(glm REQUIRED)
target_link_libraries(${LIB_NAME}
    PUBLIC
        glm          
        fmt     
)


# 4. Include Directories
target_include_directories(${LIB_NAME}
    PUBLIC
        # Allows users to #include "path_utils.hpp"
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        # Allows users to #include "core/path_utils.hpp" if they are in the root
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

# 5. Definitions inherited from root/common
# We use PUBLIC so that apps linking to core_lib also get the ROOT_DIR
target_compile_definitions(${LIB_NAME} PUBLIC 
    ROOT_DIR=\"${CMAKE_SOURCE_DIR}/\"
)

