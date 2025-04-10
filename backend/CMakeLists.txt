cmake_minimum_required(VERSION 3.10)
project(WindowHider)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force Release build
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/release)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/release)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/release)

# Set output directories for multi-configuration generators (like Visual Studio)
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/release)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/release)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/release)
endforeach(OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES)

# Common source files
set(COMMON_SOURCES
    src/window_manager.cpp
    src/injector.cpp
)

# Main application - CLI only
set(APP_SOURCES
    src/main.cpp
    ${COMMON_SOURCES}
)

# Add the main executable as a console application
add_executable(winhider ${APP_SOURCES})
target_link_libraries(winhider PRIVATE 
    psapi
    shlwapi
)
target_include_directories(winhider PRIVATE include)

# Enable Unicode in Windows
target_compile_definitions(winhider PRIVATE UNICODE _UNICODE)

# Payload DLL
set(PAYLOAD_SOURCES
    src/payload/payload.cpp
)

# Create payload DLL
add_library(payload SHARED ${PAYLOAD_SOURCES})
target_link_libraries(payload PRIVATE psapi)
target_include_directories(payload PRIVATE include)

# Ensure the payload DLL has proper export definitions
target_compile_definitions(payload PRIVATE UNICODE _UNICODE)

# Ensure the binaries are built with correct configurations
set_target_properties(winhider payload PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}
)

# Install target - simplified to a single location
install(TARGETS winhider payload
        RUNTIME DESTINATION ${CMAKE_SOURCE_DIR}/release
        LIBRARY DESTINATION ${CMAKE_SOURCE_DIR}/release) 