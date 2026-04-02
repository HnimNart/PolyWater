function(compile_slang SHADER_FILES OUTPUT_DIR SHADER_HEADERS_VAR)
  set(options )
  # ADDED: BACKEND argument to switch between Vulkan/Metal
  set(oneValueArgs BACKEND TARGET_ENV DEBUG_LEVEL OPTIMIZATION_LEVEL)
  set(multiValueArgs EXTRA_FLAGS)
  cmake_parse_arguments(COMPILE_SHADER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  # --- Defaults Setup ---
  set(BACKEND ${COMPILE_SHADER_BACKEND})
  if(NOT BACKEND)
    set(BACKEND "vulkan")
  endif()

  set(EXTRA_FLAGS ${COMPILE_SHADER_EXTRA_FLAGS})

  set(DEBUG_LEVEL ${COMPILE_SHADER_DEBUG_LEVEL})
  if(NOT COMPILE_SHADER_DEBUG_LEVEL)
    set(DEBUG_LEVEL 1)
  endif()

  set(OPTIMIZATION_LEVEL ${COMPILE_SHADER_OPTIMIZATION_LEVEL})
  if(NOT COMPILE_SHADER_OPTIMIZATION_LEVEL)
    set(OPTIMIZATION_LEVEL 0)
  endif()
   
  file(MAKE_DIRECTORY ${OUTPUT_DIR})

  set(SHADER_HEADERS "")
  set(SHADER_BINARIES "") # Tracks the .spv or .metal files

  # --- Backend-Specific Slang Flags ---
  if(BACKEND STREQUAL "metal")
      set(OUT_EXT "metal") # Use "metallib" if you want pre-compiled binaries instead of MSL source
      set(_SLANG_FLAGS
          -profile sm_6_6             # FIX: Use standard Shader Model profiles for Metal too
          -target metal               # Output MSL source
          -g${DEBUG_LEVEL}          
          -O${OPTIMIZATION_LEVEL}   
          -lang slang               
          -matrix-layout-row-major  
      )
  else()
      set(OUT_EXT "spv")
      set(_SLANG_FLAGS
          -profile sm_6_6+spirv_1_6 
          -capability spvInt64Atomics+spvShaderInvocationReorderNV+spvShaderClockKHR+spvRayTracingMotionBlurNV+spvRayQueryKHR+SPV_KHR_compute_shader_derivatives 
          -target spirv             
          -emit-spirv-directly      
          -force-glsl-scalar-layout 
          -fvk-use-entrypoint-name  
          -g${DEBUG_LEVEL}          
          -O${OPTIMIZATION_LEVEL}   
          -lang slang               
          -matrix-layout-row-major  
      )
  endif()

  foreach(SHADER ${SHADER_FILES})
      get_filename_component(SHADER_NAME ${SHADER} NAME)
      string(REPLACE "." "_" VN_SHADER_NAME ${SHADER_NAME})
      set(OUTPUT_FILE ${OUTPUT_DIR}/${SHADER_NAME})

      # --- Platform-Specific Executable Wrapping ---
      if(APPLE)
          get_filename_component(SLANG_BIN_DIR "${Slang_SLANGC_EXECUTABLE}" DIRECTORY)
          set(_SLANGC env "DYLD_LIBRARY_PATH=${SLANG_BIN_DIR}" "${Slang_SLANGC_EXECUTABLE}")
      elseif(UNIX)
          get_filename_component(SLANG_BIN_DIR "${Slang_SLANGC_EXECUTABLE}" DIRECTORY)
          set(_SLANGC env "LD_LIBRARY_PATH=${SLANG_BIN_DIR}" "${Slang_SLANGC_EXECUTABLE}")
      else()
          set(_SLANGC "${Slang_SLANGC_EXECUTABLE}")
      endif()

      # Header Generation Command (Embedded Source/Binary)
      set(_COMMAND_H ${_SLANGC}
        ${_SLANG_FLAGS} ${EXTRA_FLAGS}
        -source-embed-name ${VN_SHADER_NAME}
        -source-embed-style text # Or 'c' for hex arrays if targeting metallib
        -depfile "${OUTPUT_FILE}.dep"
        -o "${OUTPUT_FILE}.h" ${SHADER}
      )
      
      # Binary/Raw Source Generation Command (.spv or .metal)
      set(_COMMAND_S ${_SLANGC}
        ${_SLANG_FLAGS} ${EXTRA_FLAGS}
        -o "${OUTPUT_FILE}.${OUT_EXT}" ${SHADER}
      )

      list(JOIN _COMMAND_H " " _COMMAND_H_STR)

      add_custom_command(
        OUTPUT ${OUTPUT_FILE}.h ${OUTPUT_FILE}.${OUT_EXT}
        
        COMMAND $<$<CONFIG:Debug>:${CMAKE_COMMAND}> $<$<CONFIG:Debug>:-E> $<$<CONFIG:Debug>:echo> "$<$<CONFIG:Debug>:${_COMMAND_H_STR}>"
        
        COMMAND ${_COMMAND_H}
        COMMAND ${_COMMAND_S}
        MAIN_DEPENDENCY ${SHADER}
        DEPFILE "${OUTPUT_FILE}.dep"
        COMMENT "Compiling Slang shader ${SHADER_NAME} for ${BACKEND}"
        VERBATIM 
      )
      
      list(APPEND SHADER_HEADERS "${OUTPUT_FILE}.h")
      list(APPEND SHADER_BINARIES "${OUTPUT_FILE}.${OUT_EXT}")
  endforeach()

  set(${SHADER_HEADERS_VAR} ${SHADER_HEADERS} PARENT_SCOPE)
endfunction()

