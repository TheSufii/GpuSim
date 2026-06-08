#include <glad/glad.h>          
#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#include <gpusim/ParticleWorld.h>
#include <gpusim/EmitterDesc.h>
#include <gpusim/EmitterDescJson.h>
#include <gpusim/ShaderPreprocessor.h>
#include <gpusim/EmitterWidgets.h> 

#include <glm/glm.hpp>
#include "OrbitCamera.h"

#include <stb_image.h>
#include <stb_image_resize2.h>

#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kMaxParticles = 1u << 20;   // 1,048,576 (multiple of 256)

// Per-emitter lab state. `ptr` is owned by the world and stays valid for the
// world's lifetime (slots are recycled in place), so a raw pointer is safe.
struct EmitterEntry {
    ParticleEmitter* ptr = nullptr;
    EmitterDesc      desc{};
    glm::vec3        position{ 0.f };
    glm::vec3        direction{ 0.f, 1.f, 0.f };
    char             name[32] = "emitter";
};

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

GLuint CompileBillboard(const fs::path& shaderDir)
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

// One texture in the lab's registry; each becomes one layer of the world's
// texture array, so different emitters can sample different textures.
struct LoadedTexture { std::string name; std::vector<unsigned char> px; };  // res*res RGBA8

// Load + resize an image into `out` (flip + linear resize mirror the cooker).
bool LoadPixels(const char* path, uint32_t res, std::vector<unsigned char>& out)
{
    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &w, &h, &ch, 4);
    if (!data) { std::fprintf(stderr, "[lab] texture load failed: %s (%s)\n",
                              path, stbi_failure_reason()); return false; }
    out.resize((size_t)res * res * 4);
    stbir_resize_uint8_linear(data, w, h, 0, out.data(), res, res, 0, STBIR_RGBA);
    stbi_image_free(data);
    return true;
}

// (Re)build the world's texture array from the registry: layer i = textures[i].
// (Leaks the previous GL array — acceptable for an authoring tool.)
void RebuildTextureArray(ParticleWorld& world, const std::vector<LoadedTexture>& texs, uint32_t res = 256)
{
    if (texs.empty()) return;   // nothing bound → emitters fall back to procedural
    std::vector<const uint8_t*> layers;
    layers.reserve(texs.size());
    for (auto& t : texs) layers.push_back(t.px.data());
    world.CreateTextureArray(layers, res);
}

// --- ground plane + its depth target (point 4: a surface for soft particles +
// collision). We render the ground's depth into this off-screen texture — that's
// what the collision/soft passes sample — and the ground's colour to the screen
// separately, so particles never read a depth texture they're writing into. ---
struct DepthTarget {
    GLuint fbo = 0, tex = 0;
    int    w = 0, h = 0;
    void ensure(int W, int H) {
        if (fbo && W == w && H == h) return;
        destroy(); w = W; h = H;
        glCreateTextures(GL_TEXTURE_2D, 1, &tex);
        glTextureStorage2D(tex, 1, GL_DEPTH_COMPONENT32F, w, h);   // matches the engine's depth copy
        glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glCreateFramebuffers(1, &fbo);
        glNamedFramebufferTexture(fbo, GL_DEPTH_ATTACHMENT, tex, 0);
        glNamedFramebufferDrawBuffer(fbo, GL_NONE);               // depth-only
        glNamedFramebufferReadBuffer(fbo, GL_NONE);
    }
    void destroy() { if (tex) glDeleteTextures(1, &tex); if (fbo) glDeleteFramebuffers(1, &fbo); tex = fbo = 0; }
};

constexpr const char* kGroundVert = R"(#version 450 core
uniform mat4 u_viewProj;
out vec3 vWorld;
const vec2 C[6] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,-1), vec2(1,1), vec2(-1,1));
void main() {
    vec2 p = C[gl_VertexID] * 60.0;
    vWorld = vec3(p.x, 0.0, p.y);
    gl_Position = u_viewProj * vec4(vWorld, 1.0);
})";

constexpr const char* kGroundFrag = R"(#version 450 core
in vec3 vWorld;
out vec4 frag;
void main() {
    vec2  g    = abs(fract(vWorld.xz) - 0.5) / fwidth(vWorld.xz);
    float line = 1.0 - min(min(g.x, g.y), 1.0);
    frag = vec4(mix(vec3(0.10, 0.11, 0.13), vec3(0.28, 0.30, 0.34), line), 1.0);
})";

GLuint CompileProgram(const char* vs, const char* fs)
{
    GLuint v = CompileStage(GL_VERTEX_SHADER,   vs, "ground.vert");
    GLuint f = CompileStage(GL_FRAGMENT_SHADER, fs, "ground.frag");
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; glGetProgramInfoLog(p, sizeof(log), nullptr, log);
               std::fprintf(stderr, "[lab] ground link:\n%s\n", log); }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

std::vector<std::string> ScanAssets(const char* ext)
{
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(GPUSIM_LAB_ASSET_DIR, ec)) {
        if (ec) break;
        if (e.is_regular_file() && e.path().extension() == ext)
            out.push_back(e.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The editable property panel for one emitter. Scalar/enum/colour fields apply
// through Configure(); placement applies directly.
void DrawEmitterProperties(EmitterEntry& en, const std::vector<LoadedTexture>& textures)
{
    EmitterDesc& d = en.desc;
    bool ch = false;

    if (ImGui::CollapsingHeader("Emission", ImGuiTreeNodeFlags_DefaultOpen)) {
        int em = (int)d.emissionMode;
        if (ImGui::Combo("mode", &em, "Continuous\0Burst\0")) { d.emissionMode = (EmissionMode)em; ch = true; }
        if (d.emissionMode == EmissionMode::Continuous)
            ch |= ImGui::DragFloat("spawn/sec", &d.spawnRate, 5.f, 0.f, 100000.f, "%.0f");
        else {
            int bc = (int)d.burstCount;
            if (ImGui::DragInt("burst count", &bc, 1, 0, 1000000)) { d.burstCount = (uint32_t)(bc < 0 ? 0 : bc); ch = true; }
            ch |= ImGui::DragFloat("burst interval", &d.burstInterval, 0.01f, 0.f, 60.f);
        }
        ch |= ImGui::DragFloat("start delay", &d.startDelay, 0.01f, 0.f, 60.f);
        ch |= ImGui::DragFloat("duration (0=inf)", &d.duration, 0.05f, 0.f, 600.f);
        ch |= ImGui::Checkbox("active", &d.active);
    }

    if (ImGui::CollapsingHeader("Shape & launch", ImGuiTreeNodeFlags_DefaultOpen)) {
        int sh = (int)d.emissionShape;
        if (ImGui::Combo("shape", &sh, "Point\0Sphere\0Box\0Disc\0")) { d.emissionShape = (EmissionShape)sh; ch = true; }
        ch |= ImGui::DragFloat3("shape size", &d.emissionShapeSize.x, 0.02f, 0.f, 100.f);
        ch |= ImGui::SliderFloat("radial dir", &d.shapeDirection, 0.f, 1.f);
        ch |= ImGui::SliderFloat("spread", &d.spread, 0.f, 3.1416f);
        ch |= ImGui::DragFloatRange2("speed", &d.minSpeed, &d.maxSpeed, 0.05f, 0.f, 1000.f);
    }

    if (ImGui::CollapsingHeader("Life & motion", ImGuiTreeNodeFlags_DefaultOpen)) {
        ch |= ImGui::DragFloatRange2("lifetime", &d.minLifetime, &d.maxLifetime, 0.02f, 0.01f, 60.f);
        ch |= ImGui::DragFloatRange2("size", &d.minSize, &d.maxSize, 0.005f, 0.f, 50.f);
        ch |= ImGui::DragFloat("drag", &d.drag, 0.01f, 0.f, 20.f);
        ch |= ImGui::DragFloat3("gravity", &d.gravity.x, 0.05f);
        ch |= ImGui::DragFloat("velocity stretch", &d.velocityStretch, 0.005f, 0.f, 5.f);
    }

    if (ImGui::CollapsingHeader("Curl noise")) {
        ch |= ImGui::DragFloat("strength", &d.noiseStrength, 0.05f, 0.f, 50.f);
        ch |= ImGui::DragFloat("scale", &d.noiseScale, 0.05f, 0.f, 50.f);
        ch |= ImGui::DragFloat("scroll", &d.noiseScrollSpeed, 0.05f, 0.f, 50.f);
    }

    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ch |= ImGui::ColorEdit4("colour", &d.colour.x,
                                ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar);
        int bl = (int)d.blendMode;
        if (ImGui::Combo("blend", &bl, "Additive\0AlphaBlend\0")) { d.blendMode = (BlendMode)bl; ch = true; }

        // texture: procedural, or one of the loaded layers (load them in Scene → Textures)
        int curTex = (d.textureLayer == kNoTexture) ? -1 : (int)d.textureLayer;
        const char* texPreview = (curTex < 0 || curTex >= (int)textures.size())
                                 ? "procedural" : textures[curTex].name.c_str();
        if (ImGui::BeginCombo("texture", texPreview)) {
            if (ImGui::Selectable("procedural", curTex < 0)) { d.textureLayer = kNoTexture; ch = true; }
            for (int i = 0; i < (int)textures.size(); ++i)
                if (ImGui::Selectable(textures[i].name.c_str(), i == curTex)) { d.textureLayer = (uint32_t)i; ch = true; }
            ImGui::EndCombo();
        }

        int fc = (int)d.frameCount;
        if (ImGui::DragInt("frame count", &fc, 1, 1, 256)) { d.frameCount = (uint32_t)(fc < 1 ? 1 : fc); ch = true; }
        if (d.frameCount > 1) {
            int am = (int)d.animMode;
            if (ImGui::Combo("anim", &am, "PlayOnce\0Loop\0")) { d.animMode = (AnimMode)am; ch = true; }
            ch |= ImGui::DragFloat("anim fps", &d.animFps, 0.25f, 0.f, 120.f);
        }

        ImGui::Spacing();
        ch |= gpusim::DrawColourGradientEditor("colour over life", d.colourOverLife, d.colourStopCount);
        ImGui::Spacing();
        ch |= gpusim::DrawSizeCurveEditor("size over life", d.sizeOverLife, d.sizeKeyCount);
    }

    if (ImGui::CollapsingHeader("Collision & attractor")) {
        ch |= ImGui::Checkbox("collision", &d.useCollision);
        ch |= ImGui::DragFloat("restitution", &d.restitution, 0.01f, 0.f, 1.f);
        ch |= ImGui::Checkbox("attractor", &d.useAttractor);
        ch |= ImGui::DragFloat("attractor strength", &d.attractorStrength, 0.05f);
        ch |= ImGui::DragFloat3("attractor offset", &d.attractorOffset.x, 0.05f);
    }

    if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::DragFloat3("position", &en.position.x, 0.05f)) en.ptr->SetWorldPosition(en.position);
        if (ImGui::DragFloat3("direction", &en.direction.x, 0.02f)) {
            glm::vec3 dir = en.direction;
            if (glm::dot(dir, dir) > 1e-6f) en.ptr->SetWorldDirection(glm::normalize(dir));
        }
    }

    if (ch) en.ptr->Configure(d);
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

    // ---- lab state ----------------------------------------------------------
    std::vector<EmitterEntry> emitters;
    int         selected = -1;
    int         nextId   = 0;
    std::vector<LoadedTexture>  textures;       // registry; layer i = textures[i]
    std::vector<std::string>    texFiles, emitterFiles;

    auto refreshFiles = [&]() {
        texFiles     = ScanAssets(".png");
        emitterFiles = ScanAssets(".emitter");
    };
    // Find a texture in the registry by name, or load assets/<name>.png into it.
    // Returns its layer index, or -1 on failure.
    auto loadTexture = [&](const std::string& fname) -> int {
        std::string name = fs::path(fname).stem().string();
        for (int i = 0; i < (int)textures.size(); ++i)
            if (textures[i].name == name) return i;           // dedup
        LoadedTexture t; t.name = name;
        std::string full = (fs::path(GPUSIM_LAB_ASSET_DIR) / (name + ".png")).string();
        if (!LoadPixels(full.c_str(), 256, t.px)) return -1;
        textures.push_back(std::move(t));
        RebuildTextureArray(world, textures);
        return (int)textures.size() - 1;
    };
    auto addEmitter = [&]() {
        EmitterEntry en;
        en.desc.spawnRate    = 2000.f;
        en.desc.textureLayer = kNoTexture;       // procedural by default; pick a texture per emitter
        en.ptr      = world.CreateEmitter(en.desc);
        en.position = glm::vec3((float)emitters.size() * 2.f, 0.f, 0.f);
        en.ptr->SetWorldPosition(en.position);
        std::snprintf(en.name, sizeof(en.name), "emitter %d", nextId++);
        emitters.push_back(std::move(en));
        selected = (int)emitters.size() - 1;
    };
    auto removeEmitter = [&]() {
        if (selected < 0 || selected >= (int)emitters.size()) return;
        world.DestroyEmitter(emitters[selected].ptr);
        emitters.erase(emitters.begin() + selected);
        if (selected >= (int)emitters.size()) selected = (int)emitters.size() - 1;
    };
    auto loadEmitterInto = [&](EmitterEntry& en, const std::string& fname) {
        std::ifstream in(fs::path(GPUSIM_LAB_ASSET_DIR) / fname);
        if (!in) { std::fprintf(stderr, "[lab] cannot open %s\n", fname.c_str()); return; }
        nlohmann::json j;
        try { in >> j; }
        catch (const std::exception& e) { std::fprintf(stderr, "[lab] bad emitter json %s: %s\n", fname.c_str(), e.what()); return; }
        std::string tex;
        EmitterDescFromJson(j, en.desc, tex);
        int layer = tex.empty() ? -1 : loadTexture(tex);     // resolve the name into the registry
        en.desc.textureLayer = (layer >= 0) ? (uint32_t)layer : kNoTexture;
        en.ptr->Configure(en.desc);
    };
    auto saveEmitter = [&](const EmitterEntry& en, const std::string& fname) {
        std::string tex;                                      // portable name of the emitter's texture
        if (en.desc.textureLayer != kNoTexture && en.desc.textureLayer < textures.size())
            tex = textures[en.desc.textureLayer].name;
        std::ofstream out(fs::path(GPUSIM_LAB_ASSET_DIR) / fname);
        if (!out) { std::fprintf(stderr, "[lab] cannot write %s\n", fname.c_str()); return; }
        out << EmitterDescToJson(en.desc, tex).dump(2);
        refreshFiles();
    };

    refreshFiles();
    int smokeLayer = loadTexture("smoke");                 // → registry layer 0
    addEmitter();                                          // emitter 0
    emitters[0].desc.colour       = glm::vec4(0.45f, 0.72f, 1.f, 1.f);
    emitters[0].desc.useCollision = true;                  // bounces off the ground plane
    if (smokeLayer >= 0) emitters[0].desc.textureLayer = (uint32_t)smokeLayer;
    emitters[0].ptr->Configure(emitters[0].desc);

    OrbitCamera cam;

    // ground plane + its depth target (point 4)
    GLuint emptyVAO   = 0; glCreateVertexArrays(1, &emptyVAO);
    GLuint groundProg = CompileProgram(kGroundVert, kGroundFrag);
    GLint  uGroundVP  = glGetUniformLocation(groundProg, "u_viewProj");
    DepthTarget depth;
    bool  groundOn = true;
    float softFade = 2.0f;
    world.SetSoftFadeDistance(softFade);

    // ---- Main loop ----------------------------------------------------------
    uint64_t prev = SDL_GetPerformanceCounter();
    const double freq = (double)SDL_GetPerformanceFrequency();
    double   aliveTimer = 0.0;
    uint32_t aliveCount = 0;

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
        if (dt > 0.1f) dt = 0.1f;

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        const float aspect = h > 0 ? float(w) / float(h) : 1.f;
        const glm::mat4 view     = cam.view();
        const glm::mat4 proj     = cam.proj(aspect);
        const glm::mat4 viewProj = proj * view;
        world.SetCamera(view, proj);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        aliveTimer += dt;
        if (aliveTimer >= 0.25) { aliveCount = world.ReadbackAliveCount(); aliveTimer = 0.0; }


        ImGui::SetNextWindowSize(ImVec2(320, 440), ImGuiCond_FirstUseEver);
        ImGui::Begin("Scene");
        ImGui::Text("%.1f FPS  (%.2f ms)", io.Framerate, 1000.f / io.Framerate);
        ImGui::Text("alive %u / %u  (%.1f%%)", aliveCount, kMaxParticles,
                    100.f * float(aliveCount) / float(kMaxParticles));
        ImGui::Text("spawned %u / frame", world.GetLastSpawnCount());

        ImGui::SeparatorText("Ground");
        ImGui::Checkbox("ground plane + collision", &groundOn);
        if (ImGui::SliderFloat("soft fade", &softFade, 0.f, 10.f, "%.2f")) world.SetSoftFadeDistance(softFade);
        ImGui::TextDisabled("collision also needs the emitter's box");

        ImGui::SeparatorText("Textures");
        {
            static int sel = 0;
            if (sel >= (int)texFiles.size()) sel = 0;
            ImGui::SetNextItemWidth(150);
            if (ImGui::BeginCombo("##texfile", texFiles.empty() ? "<no png>" : texFiles[sel].c_str())) {
                for (int i = 0; i < (int)texFiles.size(); ++i)
                    if (ImGui::Selectable(texFiles[i].c_str(), i == sel)) sel = i;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Load##t") && !texFiles.empty()) loadTexture(texFiles[sel]);
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) refreshFiles();
            // loaded layers — each emitter picks one of these in its Appearance panel
            for (int i = 0; i < (int)textures.size(); ++i) ImGui::BulletText("%d: %s", i, textures[i].name.c_str());
            if (textures.empty()) ImGui::TextDisabled("none loaded");
        }

        ImGui::SeparatorText("Emitters");
        if (ImGui::Button("Add"))    addEmitter();
        ImGui::SameLine();
        if (ImGui::Button("Remove")) removeEmitter();
        ImGui::BeginChild("elist", ImVec2(0, 0));
        for (int i = 0; i < (int)emitters.size(); ++i) {
            ImGui::PushID(i);
            char lbl[48];
            std::snprintf(lbl, sizeof(lbl), "%s%s", emitters[i].name, emitters[i].desc.active ? "" : "  (off)");
            if (ImGui::Selectable(lbl, i == selected)) selected = i;
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::End();


        ImGui::SetNextWindowSize(ImVec2(360, 680), ImGuiCond_FirstUseEver);
        ImGui::Begin("Emitter");
        if (selected >= 0 && selected < (int)emitters.size()) {
            EmitterEntry& en = emitters[selected];
            ImGui::InputText("name", en.name, sizeof(en.name));
            if (ImGui::SmallButton("Burst 5000")) en.ptr->Burst(5000);
            ImGui::Separator();
            DrawEmitterProperties(en, textures);

            ImGui::SeparatorText("File");
            static int esel = 0;
            if (esel >= (int)emitterFiles.size()) esel = 0;
            ImGui::SetNextItemWidth(150);
            if (ImGui::BeginCombo("##efile", emitterFiles.empty() ? "<no .emitter>" : emitterFiles[esel].c_str())) {
                for (int i = 0; i < (int)emitterFiles.size(); ++i)
                    if (ImGui::Selectable(emitterFiles[i].c_str(), i == esel)) esel = i;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Load into this") && !emitterFiles.empty()) loadEmitterInto(en, emitterFiles[esel]);

            static char saveName[64] = "out.emitter";
            ImGui::SetNextItemWidth(150);
            ImGui::InputText("##savename", saveName, sizeof(saveName));
            ImGui::SameLine();
            if (ImGui::Button("Save")) saveEmitter(en, saveName);
        } else {
            ImGui::TextDisabled("no emitter selected");
        }
        ImGui::End();

        ImGui::Render();

 
        world.SetCollisionEnabled(groundOn);

        // 1) ground depth → the off-screen texture the particle passes sample
        if (groundOn) {
            depth.ensure(w, h);
            glBindFramebuffer(GL_FRAMEBUFFER, depth.fbo);
            glViewport(0, 0, w, h);
            glClear(GL_DEPTH_BUFFER_BIT);
            glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
            glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
            glUseProgram(groundProg);
            glUniformMatrix4fv(uGroundVP, 1, GL_FALSE, &viewProj[0][0]);
            glBindVertexArray(emptyVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            world.SetDepthTexture(depth.tex);
        } else {
            world.SetDepthTexture(0);
        }

        // 2) visible frame: ground to the screen, then particles on top
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        glClearColor(0.06f, 0.07f, 0.09f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (groundOn) {
            glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
            glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
            glUseProgram(groundProg);
            glUniformMatrix4fv(uGroundVP, 1, GL_FALSE, &viewProj[0][0]);
            glBindVertexArray(emptyVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        world.Dispatch(dt);
        world.Render();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // ---- Teardown -----------------------------------------------------------
    glDeleteProgram(groundProg);
    glDeleteVertexArrays(1, &emptyVAO);
    depth.destroy();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
