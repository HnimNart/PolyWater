# RtTutorial.cmake - Common CMake functions for Vulkan Raytracing Tutorial samples
#
# This file provides a reusable function to set up tutorial samples with consistent
# configuration, reducing duplication across individual CMakeLists.txt files.
#
# Usage:
#   setup_rt_tutorial_sample(
#     [USE_RT_COMMON]                    # Include RT common sources (default: OFF)
#     [USE_FOUNDATION_SHADER]            # Include foundation.slang (default: OFF)
#     [EXTRA_SHADER_INCLUDES <dirs>]     # Additional shader include directories
#     [EXTRA_COPY_FILES <files>]         # Additional files to copy
#     [EXTRA_COPY_DIRECTORIES <dirs>]    # Additional directories to copy
#     [INCLUDE_H_SLANG_FILES]            # Include .h.slang files in shader compilation
#   )

function(setup_rt_tutorial_sample)
    # Parse function arguments
    set(options USE_RT_COMMON USE_FOUNDATION_SHADER INCLUDE_H_SLANG_FILES)
    set(oneValueArgs)
    set(multiValueArgs EXTRA_SHADER_INCLUDES EXTRA_COPY_FILES EXTRA_COPY_DIRECTORIES)
    cmake_parse_arguments(RT_TUTORIAL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Get the name of the current directory
    get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
    project(${PROJECT_NAME})
    message(STATUS "Processing: ${PROJECT_NAME}")

    # Adding all sources
    file(GLOB EXE_SOURCES "*.cpp" "*.hpp" "*.md")
    source_group("Source Files" FILES ${EXE_SOURCES})

    # Handle RT common sources if requested
    set(ALL_SOURCES ${EXE_SOURCES})
    if(RT_TUTORIAL_USE_RT_COMMON)
        # Define RT common directory
        set(RT_COMMON_DIR "${TUTO_DIR}/src")

        # Add common files to make them visible in Visual Studio
        file(GLOB RT_COMMON_SOURCES "${RT_COMMON_DIR}/*.cpp" "${RT_COMMON_DIR}/*.hpp")
        source_group("RtTutorial Common" FILES ${RT_COMMON_SOURCES})
        list(APPEND ALL_SOURCES ${RT_COMMON_SOURCES})
    endif()

    # Add the executable
    add_executable(${PROJECT_NAME} ${ALL_SOURCES})
    set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "RtTutorial")

    # Link libraries and include directories (consistent across all samples)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        nvpro2::nvvk
        nvpro2::nvaftermath
        glfw   # Windowing library (Application needs it)
        glm    # Math library
        vma    # Vulkan Memory Allocator
        imgui  # Because GBuffer needs it (returns the ImTexture and uses ImGui_ImplVulkan_AddTexture
        implot # For Implot
        fmt    # For formatting strings
        common
    )

    add_project_definitions(${PROJECT_NAME})

    # Include directory for generated files
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${ROOT_DIR})

    #------------------------------------------------------------------------------------------------------------------------------
    # Installation, copy files

    # Build copy files list
    set(COPY_FILES ${NsightAftermath_DLLS})
    if(RT_TUTORIAL_EXTRA_COPY_FILES)
        list(APPEND COPY_FILES ${RT_TUTORIAL_EXTRA_COPY_FILES})
    endif()

    # Build copy directories list
    set(COPY_DIRECTORIES)
    if(RT_TUTORIAL_EXTRA_COPY_DIRECTORIES)
        list(APPEND COPY_DIRECTORIES ${RT_TUTORIAL_EXTRA_COPY_DIRECTORIES})
    endif()

    # Copy files next to the executable
    # copy_to_runtime_and_install(${PROJECT_NAME}
    #     FILES ${COPY_FILES} ${Slang_GLSLANG}
    #     DIRECTORIES ${COPY_DIRECTORIES}
    #     LOCAL_DIRS "${CMAKE_CURRENT_LIST_DIR}/shaders"
    #     AUTO
    # )
endfunction()
