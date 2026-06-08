cmake_minimum_required(VERSION 3.21)

# Enable Hot Reload / sane debug info for MSVC (mirrors DawnEngine).
if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()


set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)

project(GpuSim CXX C)

set(CMAKE_CXX_STANDARD 20)            
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TP "${CMAKE_CURRENT_SOURCE_DIR}/third_party")


find_package(OpenGL REQUIRED)


add_library(SDL3 STATIC IMPORTED)
set_target_properties(SDL3 PROPERTIES
    IMPORTED_LOCATION_DEBUG       "${TP}/SDL3/lib_debug/SDL3.lib"
    IMPORTED_LOCATION_RELEASE     "${TP}/SDL3/lib_release/SDL3.lib"
    IMPORTED_LOCATION             "${TP}/SDL3/lib_release/SDL3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${TP}/SDL3/include")

# glad — GL loader (compiled)
add_library(glad STATIC "${TP}/glad/src/glad.c")
target_include_directories(glad PUBLIC "${TP}/glad/include")

# header-only interface libs
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE "${TP}/glm")
target_compile_definitions(glm INTERFACE GLM_ENABLE_EXPERIMENTAL)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE "${TP}/nlohmann")

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
target_link_libraries(imgui PUBLIC SDL3)   # backend uses its own GL loader


add_library(GpuSim STATIC
    src/GpuSimulation.cpp
    src/ParticleEmitter.cpp
    src/ParticleWorld.cpp
    src/ParticleWorldRegistry.cpp
    src/ShaderPreprocessor.cpp)

target_include_directories(GpuSim PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/gpusim")


target_link_libraries(GpuSim PUBLIC glad glm nlohmann_json OpenGL::GL)

if(MSVC)
    target_compile_options(GpuSim PRIVATE /utf-8)
endif()


add_executable(GpuSimLab
    lab/main.cpp
    lab/stb_impl.cpp)

target_link_libraries(GpuSimLab PRIVATE GpuSim imgui SDL3 glad stb OpenGL::GL)


target_compile_definitions(GpuSimLab PRIVATE
    GPUSIM_LAB_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders/particles/")

if(MSVC)
    target_compile_options(GpuSimLab PRIVATE /utf-8)
endif()

set_target_properties(GpuSimLab PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# Runtime files next to the exe (for running outside the IDE).
if(WIN32)
    add_custom_command(TARGET GpuSimLab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${TP}/SDL3/SDL3.dll" $<TARGET_FILE_DIR:GpuSimLab>)
endif()
add_custom_command(TARGET GpuSimLab POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:GpuSimLab>/shaders")
