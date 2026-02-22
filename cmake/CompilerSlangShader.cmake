function(compile_slang SHADER_FILES OUTPUT_DIR SHADER_HEADERS_VAR)
  # ... (Argument parsing is fine) ...
  set(options )
  set(oneValueArgs TARGET_ENV DEBUG_LEVEL OPTIMIZATION_LEVEL)
  set(multiValueArgs EXTRA_FLAGS)
  cmake_parse_arguments(COMPILE_SHADER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  # ... (Defaults setup is fine) ...
  set(TARGET_ENV ${COMPILE_SHADER_TARGET_ENV})
  if(NOT TARGET_ENV)
    set(TARGET_ENV "vulkan1.4")
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
  set(SHADER_SPVS "")

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

  foreach(SHADER ${SHADER_FILES})
      get_filename_component(SHADER_NAME ${SHADER} NAME)
      string(REPLACE "." "_" VN_SHADER_NAME ${SHADER_NAME})
      set(OUTPUT_FILE ${OUTPUT_DIR}/${SHADER_NAME})

      if(UNIX)
          get_filename_component(SLANG_BIN_DIR "${Slang_SLANGC_EXECUTABLE}" DIRECTORY)
          set(_SLANGC env "LD_LIBRARY_PATH=${SLANG_BIN_DIR}" "${Slang_SLANGC_EXECUTABLE}")
      else()
          set(_SLANGC "${Slang_SLANGC_EXECUTABLE}")
      endif()

      set(_COMMAND_H ${_SLANGC}
        ${_SLANG_FLAGS} ${EXTRA_FLAGS}
        -source-embed-name ${VN_SHADER_NAME}
        -source-embed-style text
        -depfile "${OUTPUT_FILE}.dep"
        -o "${OUTPUT_FILE}.h" ${SHADER}
      )
      
      set(_COMMAND_S ${_SLANGC}
        ${_SLANG_FLAGS} ${EXTRA_FLAGS}
        -o "${OUTPUT_FILE}.spv" ${SHADER}
      )

      # FIX 1: Join the list into a string so the GenEx doesn't break
      list(JOIN _COMMAND_H " " _COMMAND_H_STR)

      add_custom_command(
        OUTPUT ${OUTPUT_FILE}.h ${OUTPUT_FILE}.spv
        
        # FIX 2: Safe generator expression using cmake -E echo
        # If Config is Debug: Runs "cmake -E echo <command_string>"
        # If Config is Release: Runs nothing (arguments become empty)
        COMMAND $<$<CONFIG:Debug>:${CMAKE_COMMAND}> $<$<CONFIG:Debug>:-E> $<$<CONFIG:Debug>:echo> "$<$<CONFIG:Debug>:${_COMMAND_H_STR}>"
        
        COMMAND ${_COMMAND_H}
        COMMAND ${_COMMAND_S}
        MAIN_DEPENDENCY ${SHADER}
        DEPFILE "${OUTPUT_FILE}.dep"
        COMMENT "Compiling Slang shader ${SHADER_NAME}"
        
        # FIX 3: VERBATIM ensures CMake escapes arguments correctly for the shell
        VERBATIM 
      )
      list(APPEND SHADER_HEADERS "${OUTPUT_FILE}.h")
  endforeach()

  set(${SHADER_HEADERS_VAR} ${SHADER_HEADERS} PARENT_SCOPE)
endfunction()

