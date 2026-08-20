cmake_minimum_required(VERSION 3.18)

project(panimpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEM_LIBS REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
    librsvg-2.0
    cairo
    tinyxml2
)
find_package(spdlog REQUIRED CONFIG)
find_package(SDL3 REQUIRED CONFIG)
find_path(WGPU_INCLUDE_DIR webgpu/wgpu.h REQUIRED)
find_library(WGPU_LIBRARY wgpu_native REQUIRED)

file(READ shaders/Compute.wgsl COMPUTE_SHADER)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    shaders/Compute.wgsl
)
configure_file(
    src/ComputeShader.hpp.in
    generated/ComputeShader.hpp
    @ONLY
)

add_compile_options(-Wall -Wextra -Wpedantic -fno-exceptions)

add_library(panim_core SHARED
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
    src/ComputeWebGpu.cpp
)
target_include_directories(panim_core
    PUBLIC include
    PRIVATE ${WGPU_INCLUDE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(panim_core
    PUBLIC
        PkgConfig::SYSTEM_LIBS
        spdlog::spdlog_header_only
        ${WGPU_LIBRARY}
)
target_compile_definitions(panim_core PUBLIC SPDLOG_NO_EXCEPTIONS=1)

if(APPLE)
    target_link_libraries(panim_core PUBLIC
        "-framework Foundation"
        "-framework CoreFoundation"
        "-framework QuartzCore"
        "-framework Metal"
    )
else()
    target_link_libraries(panim_core PUBLIC m ${CMAKE_DL_LIBS})
endif()

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set_source_files_properties(src/PreviewSurface.cpp PROPERTIES LANGUAGE OBJCXX)
endif()

add_executable(panim
    src/main.cpp
    src/Preview.cpp
    src/PreviewSurface.cpp
)
target_include_directories(panim PRIVATE ${WGPU_INCLUDE_DIR})
target_link_libraries(panim PRIVATE panim_core SDL3::SDL3 ${CMAKE_DL_LIBS})
if(APPLE)
    target_link_libraries(panim PRIVATE "-framework Cocoa")
endif()
target_compile_definitions(panim PRIVATE
    PANIM_PLUGIN_PREFIX="${CMAKE_SHARED_LIBRARY_PREFIX}"
    PANIM_PLUGIN_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}"
)
set_target_properties(panim PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

function(add_panim_plugin name)
    add_library(${name} SHARED plugins/${name}.cpp)
    target_link_libraries(${name} PRIVATE panim_core)
    set_target_properties(${name} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
    )
endfunction()

set(plugins SampleWave LatexDemo Showcase HardwareDemo FeatureTour)
foreach(plugin IN LISTS plugins)
    add_panim_plugin(${plugin})
endforeach()

enable_testing()
add_executable(panim_core_tests tests/CoreTests.cpp)
target_link_libraries(panim_core_tests PRIVATE panim_core)
add_test(NAME panim_core_tests COMMAND panim_core_tests)

include(GNUInstallDirs)
install(TARGETS panim panim_core
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(TARGETS ${plugins}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/panim/plugins
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/panim/plugins
)
install(DIRECTORY include/panim DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
