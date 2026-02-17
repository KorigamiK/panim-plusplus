cmake_minimum_required(VERSION 3.18)

project(panimpp LANGUAGES CXX)

option(PANIM_ENABLE_HWACCEL "Enable SIMD-friendly compiler flags" ON)
option(PANIM_BUILD_SAMPLE_PLUGIN "Build the bundled sample animation plugin" ON)
option(PANIM_BUILD_LATEX_DEMO "Build a LatexDemo plugin" ON)
option(PANIM_BUILD_SHOWCASE_PLUGIN "Build the Showcase timeline demo plugin" ON)
option(PANIM_ENABLE_FFMPEG "Enable FFmpeg output" ON)
option(PANIM_ENABLE_CUDA "Enable CUDA helpers" ON)
option(PANIM_USE_SPDLOG "Use spdlog for logging if available" ON)
option(PANIM_ENABLE_SVG "Enable librsvg/cairo SVG rasterizer" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(panim_core
    src/Frame.cpp
    src/PluginHost.cpp
    src/LatexRenderer.cpp
    src/VideoWriter.cpp
    src/SvgRenderer.cpp
    src/Log.cpp
    src/EquationMorph.cpp
    src/LatexTrack.cpp
    src/Painter.cpp
)

target_include_directories(panim_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(panim_core PRIVATE /W4)
else()
    target_compile_options(panim_core PRIVATE -Wall -Wextra -Wpedantic -fno-exceptions)
endif()

if(PANIM_ENABLE_HWACCEL)
    include(CheckCXXCompilerFlag)
    foreach(flag -march=native -mtune=native)
        string(REPLACE "-" "" flag_key ${flag})
        check_cxx_compiler_flag(${flag} PANIM_HAS_${flag_key})
        if(PANIM_HAS_${flag_key})
            target_compile_options(panim_core PUBLIC ${flag})
        endif()
    endforeach()
endif()

if(PANIM_ENABLE_FFMPEG)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
    pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
    pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
    pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)

    target_link_libraries(panim_core PUBLIC
        PkgConfig::AVFORMAT
        PkgConfig::AVCODEC
        PkgConfig::AVUTIL
        PkgConfig::SWSCALE
    )
    target_compile_definitions(panim_core PUBLIC PANIM_ENABLE_FFMPEG=1)
endif()

if(PANIM_ENABLE_CUDA)
    enable_language(CUDA)
    message(STATUS "CUDA support enabled")
    find_package(CUDAToolkit REQUIRED)
    add_library(panim_cuda STATIC
        src/CudaHelpers.cu
    )
    target_include_directories(panim_cuda PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(panim_cuda PUBLIC CUDA::cudart)
    target_compile_definitions(panim_cuda PUBLIC PANIM_ENABLE_CUDA=1)
    target_link_libraries(panim_core PUBLIC panim_cuda)
endif()

if(PANIM_USE_SPDLOG)
    find_package(spdlog QUIET)
    if(spdlog_FOUND)
        target_link_libraries(panim_core PUBLIC spdlog::spdlog_header_only)
        target_compile_definitions(panim_core PUBLIC PANIM_HAVE_SPDLOG=1 SPDLOG_NO_EXCEPTIONS=1)
    else()
        message(WARNING "spdlog not found; falling back to stderr logging")
    endif()
endif()

if(PANIM_ENABLE_SVG)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RSVG REQUIRED IMPORTED_TARGET librsvg-2.0)
    pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
    target_link_libraries(panim_core PUBLIC PkgConfig::RSVG PkgConfig::CAIRO)
    target_compile_definitions(panim_core PUBLIC PANIM_ENABLE_SVG=1)
endif()

add_executable(panim
    src/main.cpp
)

if(NOT MSVC)
    target_compile_options(panim PRIVATE -fno-exceptions)
endif()

target_link_libraries(panim
    PRIVATE
        panim_core
        dl
)

set_target_properties(panim PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(PANIM_BUILD_SAMPLE_PLUGIN)
    add_library(sample_wave SHARED
        plugins/SampleWave.cpp
    )

    if(NOT MSVC)
        target_compile_options(sample_wave PRIVATE -fno-exceptions)
    endif()

    target_link_libraries(sample_wave PRIVATE panim_core)

    set_target_properties(sample_wave PROPERTIES
        OUTPUT_NAME SampleWave
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
    )
endif()

if(PANIM_BUILD_LATEX_DEMO)
    add_library(latex_demo SHARED
        plugins/LatexDemo.cpp
    )
    if(NOT MSVC)
        target_compile_options(latex_demo PRIVATE -fno-exceptions)
    endif()
    target_link_libraries(latex_demo PRIVATE panim_core)
    set_target_properties(latex_demo PROPERTIES
        OUTPUT_NAME LatexDemo
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
    )
endif()

if(PANIM_BUILD_SHOWCASE_PLUGIN)
    add_library(showcase SHARED
        plugins/Showcase.cpp
    )
    if(NOT MSVC)
        target_compile_options(showcase PRIVATE -fno-exceptions)
    endif()
    target_link_libraries(showcase PRIVATE panim_core)
    set_target_properties(showcase PROPERTIES
        OUTPUT_NAME Showcase
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
    )
endif()

include(GNUInstallDirs)

install(TARGETS panim panim_core
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(PANIM_BUILD_SAMPLE_PLUGIN)
    install(TARGETS sample_wave
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/panim/plugins
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/panim/plugins
    )
endif()
