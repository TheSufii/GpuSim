// ============================================================================
//  GpuSimLab — standalone particle testbed.
//
//  A thin host around the GpuSim library: SDL3 window + GL 4.5 context + ImGui,
//  one ParticleWorld, an orbit camera. No ECS, no ResourceManager, no engine
//  renderer or lighting — the particle pass is self-contained and unlit.
//
//  This is the "walking skeleton": prove the standalone render path lights up
//  (one default emitter, procedural-circle particles) before adding the editor
//  widgets, texture loading, and .emitter load/save.
// ============================================================================
#include <glad/glad.h>          // GL loader — must precede any GL use
#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#include <gpusim/ParticleWorld.h>
#include <gpusim/EmitterDesc.h>
#include <gpusim/ShaderPreprocessor.h>

#include <glm/glm.hpp>
#include "OrbitCamera.h"

#include <cstdio>
#include <string>
#include <filesystem>

namespace {

constexpr uint32_t kMaxParticles = 1u << 20;   // 1,048,576 (multiple of 256)

// Compile one GLSL stage from already-preprocessed source.
GLuint CompileStage(GLenum type, const std::string& src, const char* tag)
{
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof(log), nullptr, log);
               std::fprintf(stderr, "[lab] %s compile failed:\n%s\n", tag, log); }
    return s;
}

// The host compiles the billboard shader and hands the program to SetupDefault.
// The .vert/.frag #include "common.glsl", so run them through the library's own
// preprocessor first (it also lifts #version to the top).
GLuint CompileBillboard(const std::filesystem::path& shaderDir)
{
    auto v = GPUSim::PreprocessShader(shaderDir / "particle_billboard.vert");
    auto f = GPUSim::PreprocessShader(shaderDir / "particle_billboard.frag");
    if (!v.success) { std::fprintf(stderr, "[lab] vert preprocess: %s\n", v.error.c_str()); return 0; }
    if (!f.success) { std::fprintf(stderr, "[lab] frag preprocess: %s\n", f.error.c_str()); return 0; }

    GLuint vs = CompileStage(GL_VERTEX_SHADER,   v.source, "billboard.vert");
    GLuint fs = CompileStage(GL_FRAGMENT_SHADER, f.source, "billboard.frag");

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
               std::fprintf(stderr, "[lab] billboard link failed:\n%s\n", log); }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

} // namespace

int main(int, char**)
{
    // ---- SDL + GL 4.5 core context -----------------------------------------
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[lab] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow("GpuSimLab", 1600, 900,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) { std::fprintf(stderr, "[lab] CreateWindow failed: %s\n", SDL_GetError()); return 1; }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { std::fprintf(stderr, "[lab] GL context failed: %s\n", SDL_GetError()); return 1; }
    SDL_GL_MakeCurrent(window, gl);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::fprintf(stderr, "[lab] gladLoadGLLoader failed\n");
        return 1;
    }
    SDL_GL_SetSwapInterval(1);   // vsync

    // ---- ImGui (docking) ----------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 450");

    // ---- Particle world -----------------------------------------------------
    GLuint billboard = CompileBillboard(GPUSIM_LAB_SHADER_DIR);

    ParticleWorld world;
    world.Initialize(kMaxParticles, GPUSIM_LAB_SHADER_DIR);
    world.SetupDefault(billboard);
    world.SetNearFar(0.1f, 1000.f);

    // One default emitter — a visible upward jet from the origin, no texture
    // (the billboard shader draws a procedural soft circle when none is bound).
    EmitterDesc desc{};
    desc.spawnRate = 4000.f;
    desc.minSpeed  = 3.f;  desc.maxSpeed  = 6.f;
    desc.minSize   = 0.06f; desc.maxSize  = 0.16f;
    desc.minLifetime = 1.5f; desc.maxLifetime = 3.0f;
    desc.colour    = glm::vec4(0.45f, 0.72f, 1.0f, 1.0f);
    ParticleEmitter* emitter = world.CreateEmitter(desc);
    emitter->SetWorldPosition(glm::vec3(0.f));

    OrbitCamera cam;

    // ---- Main loop ----------------------------------------------------------
    uint64_t prev = SDL_GetPerformanceCounter();
    const double freq = (double)SDL_GetPerformanceFrequency();

    float    aliveDisplay = 0.f;   // throttled readback cache (the readback stalls)
    double   aliveTimer   = 0.0;
    uint32_t aliveCount   = 0;

    bool running = true;
    while (running)
    {
        const ImGuiIO& io = ImGui::GetIO();

        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT) running = false;
            else if (ev.type == SDL_EVENT_MOUSE_MOTION && !io.WantCaptureMouse &&
                     (ev.motion.state & SDL_BUTTON_LMASK))
                cam.orbit(-ev.motion.xrel * 0.005f, -ev.motion.yrel * 0.005f);
            else if (ev.type == SDL_EVENT_MOUSE_WHEEL && !io.WantCaptureMouse)
                cam.zoom(ev.wheel.y * 0.8f);
        }

        const uint64_t now = SDL_GetPerformanceCounter();
        float dt = float((now - prev) / freq);
        prev = now;
        if (dt > 0.1f) dt = 0.1f;   // clamp after stalls / window drags

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        const float aspect = h > 0 ? float(w) / float(h) : 1.f;

        world.SetCamera(cam.view(), cam.proj(aspect));

        // ---- UI ----
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        aliveTimer += dt;
        if (aliveTimer >= 0.25) { aliveCount = world.ReadbackAliveCount(); aliveTimer = 0.0; }

        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("GpuSimLab");
        ImGui::Text("%.1f FPS  (%.2f ms)", io.Framerate, 1000.f / io.Framerate);
        ImGui::Separator();
        ImGui::Text("alive    : %u", aliveCount);
        ImGui::Text("capacity : %u", kMaxParticles);
        ImGui::Text("util     : %.1f%%", 100.f * float(aliveCount) / float(kMaxParticles));
        ImGui::Text("emitters : %u", world.GetEmitterCount());
        ImGui::Text("spawned  : %u / frame", world.GetLastSpawnCount());
        ImGui::Separator();
        static float rate = desc.spawnRate;
        if (ImGui::SliderFloat("spawn/sec", &rate, 0.f, 50000.f, "%.0f"))
            emitter->SetSpawnRate(rate);
        if (ImGui::Button("Burst 5000")) emitter->Burst(5000);
        ImGui::TextDisabled("LMB drag: orbit   wheel: zoom");
        ImGui::End();

        ImGui::Render();

        // ---- Frame ----
        glViewport(0, 0, w, h);
        glClearColor(0.06f, 0.07f, 0.09f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        world.Dispatch(dt);
        world.Render();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // ---- Teardown -----------------------------------------------------------
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
