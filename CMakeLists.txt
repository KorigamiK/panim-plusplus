cmake_minimum_required(VERSION 3.18)

project(panimpp LANGUAGES CXX)

option(PANIM_ENABLE_HWACCEL "Enable SIMD-friendly compiler flags" ON)
option(PANIM_BUILD_SAMPLE_PLUGIN "Build the bundled sample animation plugin" ON)
option(PANIM_BUILD_LATEX_DEMO "Build a LatexDemo plugin" ON)
option(PANIM_BUILD_SHOWCASE_PLUGIN "Build the Showcase timeline demo plugin" ON)
option(PANIM_BUILD_HARDWARE_DEMO "Build the cross-platform compute demo plugin" ON)
option(PANIM_BUILD_FEATURE_TOUR "Build the complete feature-tour animation" ON)
option(PANIM_BUILD_TESTS "Build lightweight core tests" ON)
option(PANIM_ENABLE_FFMPEG "Enable FFmpeg output" ON)
option(PANIM_ENABLE_WEBGPU "Enable portable WebGPU/WGSL compute" ON)
option(PANIM_BUILD_PREVIEW "Build the SDL3/WebGPU interactive preview" ON)
option(PANIM_USE_SPDLOG "Use spdlog for logging if available" ON)
option(PANIM_ENABLE_SVG "Enable librsvg/cairo SVG rasterizer" ON)
set(PANIM_WGPU_ROOT "" CACHE PATH "Root of an installed wgpu-native distribution")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(panim_core
    src/Frame.cpp
    src/PluginHost.cpp
    src/LatexRenderer.cpp
    src/VideoWriter.cpp
    src/FrameSink.cpp
    src/RenderSession.cpp
    src/SvgRenderer.cpp
    src/Log.cpp
    src/EquationMorph.cpp
    src/LatexTrack.cpp
    src/Painter.cpp
    src/SceneSequence.cpp
    src/Compute.cpp
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

if(PANIM_ENABLE_WEBGPU)
    find_path(PANIM_WGPU_INCLUDE_DIR
        NAMES webgpu/webgpu.h webgpu/wgpu.h
        HINTS ${PANIM_WGPU_ROOT} $ENV{PANIM_WGPU_ROOT}
        PATH_SUFFIXES include
    )
    find_library(PANIM_WGPU_LIBRARY
        NAMES libwgpu_native.a wgpu_native
        HINTS ${PANIM_WGPU_ROOT} $ENV{PANIM_WGPU_ROOT}
        PATH_SUFFIXES lib
    )

    if(PANIM_WGPU_INCLUDE_DIR AND PANIM_WGPU_LIBRARY)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/Compute.wgsl
        )
        file(READ
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/Compute.wgsl
            PANIM_COMPUTE_WGSL
        )
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/src/ComputeShader.hpp.in
            ${CMAKE_CURRENT_BINARY_DIR}/generated/ComputeShader.hpp
            @ONLY
        )
        target_sources(panim_core PRIVATE src/ComputeWebGpu.cpp)
        target_include_directories(panim_core PRIVATE
            ${PANIM_WGPU_INCLUDE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/generated
        )
        target_compile_definitions(panim_core PUBLIC PANIM_HAVE_WEBGPU=1)
        target_link_libraries(panim_core PUBLIC ${PANIM_WGPU_LIBRARY})

        if(WIN32)
            target_link_libraries(panim_core PUBLIC
                d3dcompiler ws2_32 userenv bcrypt ntdll opengl32
                Propsys RuntimeObject
            )
        elseif(APPLE)
            find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
            find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
            find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
            find_library(METAL_FRAMEWORK Metal REQUIRED)
            target_link_libraries(panim_core PUBLIC
                ${FOUNDATION_FRAMEWORK}
                ${COREFOUNDATION_FRAMEWORK}
                ${QUARTZCORE_FRAMEWORK}
                ${METAL_FRAMEWORK}
            )
        elseif(UNIX)
            target_link_libraries(panim_core PUBLIC m ${CMAKE_DL_LIBS})
        endif()

        message(STATUS
            "WebGPU/WGSL compute enabled: ${PANIM_WGPU_LIBRARY}"
        )
    else()
        message(WARNING
            "wgpu-native was not found; compute will use the CPU fallback. "
            "Set PANIM_WGPU_ROOT to an installed wgpu-native distribution."
        )
    endif()
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
    pkg_check_modules(TINYXML2 REQUIRED IMPORTED_TARGET tinyxml2)
    target_link_libraries(panim_core PUBLIC
        PkgConfig::RSVG
        PkgConfig::CAIRO
        PkgConfig::TINYXML2
    )
    target_compile_definitions(panim_core PUBLIC PANIM_ENABLE_SVG=1)
endif()

add_executable(panim
    src/main.cpp
)

if(PANIM_BUILD_PREVIEW)
    set(PANIM_SDL_TARGET "")
    find_package(SDL3 QUIET CONFIG)
    if(TARGET SDL3::SDL3)
        set(PANIM_SDL_TARGET SDL3::SDL3)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL3_PKG QUIET IMPORTED_TARGET sdl3)
        if(SDL3_PKG_FOUND)
            set(PANIM_SDL_TARGET PkgConfig::SDL3_PKG)
        endif()
    endif()
    if(PANIM_SDL_TARGET AND PANIM_WGPU_INCLUDE_DIR AND PANIM_WGPU_LIBRARY)
        if(APPLE)
            enable_language(OBJCXX)
            set_source_files_properties(src/PreviewSurface.cpp
                PROPERTIES LANGUAGE OBJCXX
            )
            find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
            target_link_libraries(panim PRIVATE ${COCOA_FRAMEWORK})
        endif()
        target_sources(panim PRIVATE
            src/Preview.cpp
            src/PreviewSurface.cpp
        )
        target_include_directories(panim PRIVATE ${PANIM_WGPU_INCLUDE_DIR})
        target_link_libraries(panim PRIVATE ${PANIM_SDL_TARGET})
        target_compile_definitions(panim PRIVATE PANIM_HAVE_PREVIEW=1)
        message(STATUS "Interactive preview enabled with system SDL3")
    else()
        message(WARNING
            "Interactive preview disabled; SDL3 and wgpu-native are required"
        )
    endif()
endif()

if(NOT MSVC)
    target_compile_options(panim PRIVATE -fno-exceptions)
endif()

target_link_libraries(panim
    PRIVATE
        panim_core
        ${CMAKE_DL_LIBS}
)

target_compile_definitions(panim PRIVATE
    PANIM_PLUGIN_PREFIX="${CMAKE_SHARED_LIBRARY_PREFIX}"
    PANIM_PLUGIN_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}"
)

set_target_properties(panim PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

function(panim_add_plugin target source output_name)
    add_library(${target} SHARED ${source})
    target_link_libraries(${target} PRIVATE panim_core)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -fno-exceptions)
    endif()
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME ${output_name}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
    )
endfunction()

if(PANIM_BUILD_SAMPLE_PLUGIN)
    panim_add_plugin(sample_wave plugins/SampleWave.cpp SampleWave)
    add_custom_target(SampleWave DEPENDS sample_wave)
endif()

if(PANIM_BUILD_LATEX_DEMO)
    panim_add_plugin(latex_demo plugins/LatexDemo.cpp LatexDemo)
    add_custom_target(LatexDemo DEPENDS latex_demo)
endif()

if(PANIM_BUILD_SHOWCASE_PLUGIN)
    panim_add_plugin(showcase plugins/Showcase.cpp Showcase)
    add_custom_target(Showcase DEPENDS showcase)
endif()

if(PANIM_BUILD_HARDWARE_DEMO)
    panim_add_plugin(hardware_demo plugins/HardwareDemo.cpp HardwareDemo)
    add_custom_target(HardwareDemo DEPENDS hardware_demo)
endif()

if(PANIM_BUILD_FEATURE_TOUR)
    panim_add_plugin(feature_tour plugins/FeatureTour.cpp FeatureTour)
    add_custom_target(FeatureTour DEPENDS feature_tour)
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

if(PANIM_BUILD_LATEX_DEMO)
    install(TARGETS latex_demo
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/panim/plugins
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/panim/plugins
    )
endif()

if(PANIM_BUILD_SHOWCASE_PLUGIN)
    install(TARGETS showcase
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/panim/plugins
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/panim/plugins
    )
endif()

if(PANIM_BUILD_HARDWARE_DEMO)
    install(TARGETS hardware_demo
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/panim/plugins
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/panim/plugins
    )
endif()

if(PANIM_BUILD_FEATURE_TOUR)
    install(TARGETS feature_tour
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/panim/plugins
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/panim/plugins
    )
endif()

install(DIRECTORY include/panim
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

include(CTest)
if(BUILD_TESTING AND PANIM_BUILD_TESTS)
    add_executable(panim_core_tests tests/CoreTests.cpp)
    target_link_libraries(panim_core_tests PRIVATE panim_core)
    if(NOT MSVC)
        target_compile_options(panim_core_tests PRIVATE -fno-exceptions)
    endif()
    add_test(NAME panim_core_tests COMMAND panim_core_tests)
endif()
