# Internal helper function to avoid logic duplication
function(_compile_slang_internal SHADER_FILES OUTPUT_DIR SHADER_HEADERS_VAR BACKEND)
  set(options)
  set(oneValueArgs DEBUG_LEVEL OPTIMIZATION_LEVEL)
  set(multiValueArgs EXTRA_FLAGS SLANG_FLAGS)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  file(MAKE_DIRECTORY ${OUTPUT_DIR})

  set(DEBUG_LEVEL ${ARG_DEBUG_LEVEL})
  if(NOT DEBUG_LEVEL)
    set(DEBUG_LEVEL 1)
  endif()

  set(OPTIMIZATION_LEVEL ${ARG_OPTIMIZATION_LEVEL})
  if(NOT OPTIMIZATION_LEVEL)
    set(OPTIMIZATION_LEVEL 0)
  endif()

  set(OUT_EXT "spv")
  if(BACKEND STREQUAL "metal")
    set(OUT_EXT "metal")
  endif()

  set(SHADER_HEADERS "")

  foreach(SHADER ${SHADER_FILES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    string(REPLACE "." "_" VN_SHADER_NAME ${SHADER_NAME})
    set(OUTPUT_FILE ${OUTPUT_DIR}/${SHADER_NAME})

    # Platform-Specific Executable Wrapping
    if(APPLE)
      get_filename_component(SLANG_BIN_DIR "${Slang_SLANGC_EXECUTABLE}" DIRECTORY)
      set(_SLANGC env "DYLD_LIBRARY_PATH=${SLANG_BIN_DIR}" "${Slang_SLANGC_EXECUTABLE}")
    elseif(UNIX)
      get_filename_component(SLANG_BIN_DIR "${Slang_SLANGC_EXECUTABLE}" DIRECTORY)
      set(_SLANGC env "LD_LIBRARY_PATH=${SLANG_BIN_DIR}" "${Slang_SLANGC_EXECUTABLE}")
    else()
      set(_SLANGC "${Slang_SLANGC_EXECUTABLE}")
    endif()

    set(BASE_FLAGS 
      ${ARG_SLANG_FLAGS} 
      ${ARG_EXTRA_FLAGS} 
      -g${DEBUG_LEVEL} 
      -O${OPTIMIZATION_LEVEL}
      -lang slang
      -matrix-layout-row-major
    )

    add_custom_command(
      OUTPUT "${OUTPUT_FILE}.h" "${OUTPUT_FILE}.${OUT_EXT}"
      COMMAND ${_SLANGC} ${BASE_FLAGS} -source-embed-name ${VN_SHADER_NAME} -source-embed-style text -depfile "${OUTPUT_FILE}.dep" -o "${OUTPUT_FILE}.h" ${SHADER}
      COMMAND ${_SLANGC} ${BASE_FLAGS} -o "${OUTPUT_FILE}.${OUT_EXT}" ${SHADER}
      MAIN_DEPENDENCY ${SHADER}
      DEPFILE "${OUTPUT_FILE}.dep"
      COMMENT "Compiling Slang shader ${SHADER_NAME} for ${BACKEND}"
      VERBATIM
    )
    
    list(APPEND SHADER_HEADERS "${OUTPUT_FILE}.h")
  endforeach()

  set(${SHADER_HEADERS_VAR} ${SHADER_HEADERS} PARENT_SCOPE)
endfunction()

# 1. Specialized function for Metal
function(compile_slang_to_metal SHADER_FILES OUTPUT_DIR SHADER_HEADERS_VAR)
  set(_METAL_FLAGS
    -profile sm_6_6  # Modern features that map to Metal 3.0+
    -target metal
  )
  _compile_slang_internal("${SHADER_FILES}" "${OUTPUT_DIR}" ${SHADER_HEADERS_VAR} "metal" SLANG_FLAGS ${_METAL_FLAGS} ${ARGN})
  set(${SHADER_HEADERS_VAR} ${${SHADER_HEADERS_VAR}} PARENT_SCOPE)
endfunction()

# 2. Specialized function for SPIR-V (Vulkan)
function(compile_slang_to_spirv SHADER_FILES OUTPUT_DIR SHADER_HEADERS_VAR)
  set(_SPIRV_FLAGS
    -profile sm_6_6+spirv_1_6 
    -capability spvInt64Atomics+spvShaderInvocationReorderNV+spvShaderClockKHR+spvRayTracingMotionBlurNV+spvRayQueryKHR+SPV_KHR_compute_shader_derivatives 
    -target spirv             
    -emit-spirv-directly      
    -force-glsl-scalar-layout 
    -fvk-use-entrypoint-name
  )
  _compile_slang_internal("${SHADER_FILES}" "${OUTPUT_DIR}" ${SHADER_HEADERS_VAR} "vulkan" SLANG_FLAGS ${_SPIRV_FLAGS} ${ARGN})
  set(${SHADER_HEADERS_VAR} ${${SHADER_HEADERS_VAR}} PARENT_SCOPE)
endfunction()
