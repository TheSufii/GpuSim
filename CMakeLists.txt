# ============================================================================
#  GpuSim — standalone GPU particle library + testbed (GpuSimLab)
#
#  Builds two ways:
#   • Standalone (this repo is the top-level CMake project): builds the GpuSim
#     library AND the GpuSimLab testbed, vendoring SDL3 + ImGui + stb.
#   • As a subproject (a host engine does add_subdirectory() on this repo):
#     builds ONLY the GpuSim library. It reuses the host's glad/glm/nlohmann
#     targets when they already exist — critically glad, so the GL function-
#     pointer table stays a single shared instance. The testbed and its
#     window/UI deps (SDL3, ImGui, stb) are skipped.
# ============================================================================
cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)

project(GpuSim CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TP "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# ----------------------------------------------------------------------------
#  Library dependencies — shared with the host when consumed as a subproject.
#  The if(NOT TARGET ...) guards avoid duplicate-target errors and (for glad)
#  keep the GL loader a single instance across the host + library.
# ----------------------------------------------------------------------------
if(NOT TARGET glad)
    add_library(glad STATIC "${TP}/glad/src/glad.c")
    target_include_directories(glad PUBLIC "${TP}/glad/include")
endif()
if(NOT TARGET glm)
    add_library(glm INTERFACE)
    target_include_directories(glm INTERFACE "${TP}/glm")
    target_compile_definitions(glm INTERFACE GLM_ENABLE_EXPERIMENTAL)
endif()
if(NOT TARGET nlohmann_json)
    add_library(nlohmann_json INTERFACE)
    target_include_directories(nlohmann_json INTERFACE "${TP}/nlohmann")
endif()

# ----------------------------------------------------------------------------
#  GpuSim — the library (always built)
# ----------------------------------------------------------------------------
add_library(GpuSim STATIC
    src/GpuSimulation.cpp
    src/ParticleEmitter.cpp
    src/ParticleWorld.cpp
    src/ParticleWorldRegistry.cpp
    src/ShaderPreprocessor.cpp)

# include/        → consumers: #include <gpusim/ParticleWorld.h>
# include/gpusim/ → internal + flat cross-header includes ("ParticleWorld.h")
target_include_directories(GpuSim PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/gpusim")
target_link_libraries(GpuSim PUBLIC glad glm nlohmann_json)
if(MSVC)
    target_compile_options(GpuSim PRIVATE /utf-8)
endif()

# ----------------------------------------------------------------------------
#  Standalone-only: the testbed + the window/UI deps only it needs.
#  Skipped entirely when a host engine consumes this via add_subdirectory().
# ----------------------------------------------------------------------------
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    find_package(OpenGL REQUIRED)

    add_library(SDL3 STATIC IMPORTED)
    set_target_properties(SDL3 PROPERTIES
        IMPORTED_LOCATION_DEBUG       "${TP}/SDL3/lib_debug/SDL3.lib"
        IMPORTED_LOCATION_RELEASE     "${TP}/SDL3/lib_release/SDL3.lib"
        IMPORTED_LOCATION             "${TP}/SDL3/lib_release/SDL3.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${TP}/SDL3/include")

    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE "${TP}/stb")

    add_library(imgui STATIC
        "${TP}/imgui/imgui.cpp"
        "${TP}/imgui/imgui_draw.cpp"
        "${TP}/imgui/imgui_widgets.cpp"
        "${TP}/imgui/imgui_tables.cpp"
        "${TP}/imgui/imgui_demo.cpp"
        "${TP}/imgui/imgui_impl_sdl3.cpp"
        "${TP}/imgui/imgui_impl_opengl3.cpp")
    target_include_directories(imgui PUBLIC "${TP}/imgui")
    target_link_libraries(imgui PUBLIC SDL3)

    add_executable(GpuSimLab lab/main.cpp lab/stb_impl.cpp)
    target_link_libraries(GpuSimLab PRIVATE GpuSim imgui SDL3 glad stb OpenGL::GL)
    target_compile_definitions(GpuSimLab PRIVATE
        GPUSIM_LAB_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders/particles/"
        GPUSIM_LAB_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lab/assets/")
    if(MSVC)
        target_compile_options(GpuSimLab PRIVATE /utf-8)
    endif()
    set_target_properties(GpuSimLab PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    if(WIN32)
        add_custom_command(TARGET GpuSimLab POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TP}/SDL3/SDL3.dll" $<TARGET_FILE_DIR:GpuSimLab>)
    endif()
    add_custom_command(TARGET GpuSimLab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:GpuSimLab>/shaders")
    add_custom_command(TARGET GpuSimLab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/lab/assets" "$<TARGET_FILE_DIR:GpuSimLab>/assets")
endif()
