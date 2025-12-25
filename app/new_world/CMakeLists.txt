# Setup with foundation shader and extra includes
setup_rt_tutorial_sample(
    USE_FOUNDATION_SHADER
    EXTRA_SHADER_INCLUDES
        ${COMMON_DIR}/shaders
    EXTRA_COPY_DIRECTORIES
        ${COMMON_DIR}/shaders
)
