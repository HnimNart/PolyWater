cmake_minimum_required(VERSION 3.20)

# Root project (keeps name generic)
project(engine LANGUAGES CXX)

# Build shared/static based on user preference
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# Delegate to the engine folder
add_subdirectory(engine)
