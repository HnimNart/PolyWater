# Metal backend - Apple platforms only
if(NOT APPLE)
  return()
endif()

# -------------------------------------------------------------------
# metal_backend static library
# -------------------------------------------------------------------
set(LIB_NAME metal_backend)

file(GLOB_RECURSE METAL_SOURCES CONFIGURE_DEPENDS
    "core/*.hpp"
    "core/*.mm"
    "gui/*.hpp"
    "gui/*.mm"
)

add_library(${LIB_NAME} STATIC ${METAL_SOURCES})

# -------------------------------------------------------------------
# Include Directories
# -------------------------------------------------------------------
target_include_directories(${LIB_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/>
        ${SHADER_OUTPUT_DIR}/../
        ${SHADER_OUTPUT_DIR}
)

# -------------------------------------------------------------------
# Linking
# -------------------------------------------------------------------
target_link_libraries(${LIB_NAME}
    PUBLIC
        backend_interfaces
    PRIVATE
        core_lib
        glfw
        imgui
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Cocoa"
)

# -------------------------------------------------------------------
# IDE Organization
# -------------------------------------------------------------------
set_property(TARGET ${LIB_NAME} PROPERTY FOLDER "Backends")
source_group("core" REGULAR_EXPRESSION "core/.*")
source_group("gui"  REGULAR_EXPRESSION "gui/.*")
