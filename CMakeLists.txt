cmake_minimum_required(VERSION 3.10)

# Project name and the type of project (C++ in this case)
project(rpicam_hello VERSION 1.0 LANGUAGES CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror -O3 -pedantic -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-sign-compare -faligned-new -D_FILE_OFFSET_BITS=64 -Wno-psabi -DLIBEGL_PRESENT=1")

# Include directories
include_directories(${CMAKE_SOURCE_DIR} /usr/include/libcamera)

# Source files for the rpicam_hello application
set(SOURCES
    apps/rpicam_hello.cpp
    apps/rotary_encoder.cpp
    apps/RED.cpp
)

# Define the executable to be built from the specified source files
add_executable(saturn-app.bin ${SOURCES})

# Find libraries
find_library(PIGPIO_LIBRARY pigpio PATHS /usr/lib)
find_library(PIGPIOD_IF2_LIBRARY pigpiod_if2 PATHS /usr/lib)
find_library(CAMERA_LIB libcamera.so PATHS /usr/lib/arm-linux-gnueabihf)
find_library(CAMERA_BASE_LIB libcamera-base.so PATHS /usr/lib/arm-linux-gnueabihf)

# Assuming rpicam_app.so is located in the apps/build directory relative to the project root
find_library(RPICAM_APP_LIB NAMES rpicam_app.so PATHS ${CMAKE_SOURCE_DIR}/apps/build NO_DEFAULT_PATH)

# Link the executable with the required libraries
target_link_libraries(saturn-app.bin
    ${PIGPIO_LIBRARY}
    ${PIGPIOD_IF2_LIBRARY}
    ${CAMERA_LIB}
    ${CAMERA_BASE_LIB}
    pthread # For pthread
    ${RPICAM_APP_LIB} # Use the found library path for rpicam_app.so
)
