cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()
project(GpuSim CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TP "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

if(MSVC AND PROJECT_IS_TOP_LEVEL)
    option(GPUSIM_STATIC_CRT "Link the static MSVC runtime so the build needs no VC++ redistributable" ON)
    if(GPUSIM_STATIC_CRT)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()

option(GPUSIM_EXTERNAL_GLAD "Use a pre-defined glad_gl target instead of internal GLAD" OFF)

if (GPUSIM_EXTERNAL_GLAD)
    if(NOT TARGET glad_gl)
        message(FATAL_ERROR
                "[GPUSIM] GPUSIM is ON but glad_gl target not found. Make sure you define it before adding GpuSim.")
    endif()
else()
    if(NOT TARGET glad_gl)
        add_library(glad_gl STATIC "${TP}/glad/src/gl.c")
        target_include_directories(glad_gl PUBLIC "${TP}/glad/include")
    endif()
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


add_library(GpuSim STATIC
    src/GpuSimulation.cpp
    src/ParticleEmitter.cpp
    src/ParticleWorld.cpp
    src/ParticleWorldRegistry.cpp
    src/ShaderPreprocessor.cpp)


target_include_directories(GpuSim PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/gpusim")
target_link_libraries(GpuSim PUBLIC glad_gl glm nlohmann_json)
if(MSVC)
    target_compile_options(GpuSim PRIVATE /utf-8)
endif()


target_compile_definitions(GpuSim INTERFACE
    GPUSIM_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders/particles/")


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
    target_link_libraries(GpuSimLab PRIVATE GpuSim imgui SDL3 glad_gl stb OpenGL::GL)

 
    option(GPUSIM_EMBED_SOURCE_PATHS "Bake the source-tree data paths in as a dev fallback" ON)
    if(GPUSIM_EMBED_SOURCE_PATHS)
        target_compile_definitions(GpuSimLab PRIVATE
            GPUSIM_LAB_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders/particles/"
            GPUSIM_LAB_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lab/assets/")
    else()
        target_compile_definitions(GpuSimLab PRIVATE
            GPUSIM_LAB_SHADER_DIR=""
            GPUSIM_LAB_ASSET_DIR="")
    endif()
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


    install(TARGETS GpuSimLab RUNTIME DESTINATION .)
    install(FILES "${TP}/SDL3/SDL3.dll" DESTINATION .)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/shaders/"    DESTINATION shaders)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lab/assets/" DESTINATION assets)
    if(MSVC AND NOT GPUSIM_STATIC_CRT)
        # DLL runtime: ship the redistributable DLLs so the package still runs
        # on a machine without the matching VC++ redistributable installed.
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
        include(InstallRequiredSystemLibraries)
    endif()

    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_NAME "GpuSimLab")
    set(CPACK_PACKAGE_FILE_NAME "GpuSimLab-win64")
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
    include(CPack)
endif()
