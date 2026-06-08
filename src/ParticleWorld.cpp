#include "ParticleWorld.h"
#include "ParticleWorldRegistry.h"
#include "SimBindings.h"
#include <cassert>
#include <vector>
#include "log.h"   // gpusim-local logging (was engine's logging/log.h → spdlog)

// --------------------------------------------------------
// internal radix SortStage — always runs. owns the three radix programs.
// --------------------------------------------------------
class SortStage : public SimStage
{
public:
    SortStage(GLuint count, GLuint prefix, GLuint scatter, uint32_t maxElements)
        : m_count(count), m_prefix(prefix), m_scatter(scatter), m_max(maxElements)
    {
        assert(count && prefix && scatter && "SortStage: invalid shaders");
    }

    void Execute(const SimContext& ctx)
    {
        const uint32_t groups = m_max / 256;
        for (uint32_t pass = 0; pass < 4; ++pass)
        {
            uint32_t shift = pass * 8;
            uint32_t pingPong = pass & 1;

            glUseProgram(m_count);
            glUniform1ui(glGetUniformLocation(m_count, "u_bitShift"), shift);
            glUniform1ui(glGetUniformLocation(m_count, "u_pingPong"), pingPong);  // <-- added
            glDispatchCompute(groups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            glUseProgram(m_prefix);
            glDispatchCompute(1, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            glUseProgram(m_scatter);
            glUniform1ui(glGetUniformLocation(m_scatter, "u_bitShift"), shift);
            glUniform1ui(glGetUniformLocation(m_scatter, "u_pingPong"), pingPong);
            glDispatchCompute(groups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
    }

    // last stage before the draw: vertex shader reads SSBOs, indirect is a draw
    // command, and the frag samples the depth texture for soft particles.
    GLbitfield GetBarrierBits() const override
    {
        return GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT;
    }

private:
    GLuint m_count, m_prefix, m_scatter;
    uint32_t m_max;
};

// --------------------------------------------------------
ParticleWorld::~ParticleWorld()
{
    if (m_registry) m_registry->Unregister(this);

    GLuint bufs[] = { m_deadListBuffer, m_deadCountBuffer, m_sortBuffer,
                      m_sortBufferAlt, m_histogramBuffer, m_emitterBuffer,
                      m_worldUBO, m_cameraUBO };
    for (GLuint b : bufs) if (b) glDeleteBuffers(1, &b);

    if (m_ownsTextureArray && m_textureArray)
        glDeleteTextures(1, &m_textureArray);

    if (m_lutColour) glDeleteTextures(1, &m_lutColour);
    if (m_lutScalar) glDeleteTextures(1, &m_lutScalar);
}

void ParticleWorld::Initialize(uint32_t maxParticles, const char* shaderDir)
{
    assert(maxParticles % k_threadGroup == 0 &&
        "maxParticles must be a multiple of 256");
    assert(shaderDir != nullptr);

    m_shaderDir = shaderDir;
    GpuSimulation::Initialize(maxParticles, sizeof(Particle));

    InitializeDeadList();
    InitializeSortBuffers();
    InitializeEmitterBuffer();
    InitializeUBOs();

    AddAuxBuffer(m_deadListBuffer, SimBindings::DeadList);
    AddAuxBuffer(m_deadCountBuffer, SimBindings::DeadCount);
    AddAuxBuffer(m_sortBuffer, SimBindings::SortKeys);
    AddAuxBuffer(m_sortBufferAlt, SimBindings::SortKeysAlt);
    AddAuxBuffer(m_histogramBuffer, SimBindings::SortHistogram);
    AddAuxBuffer(m_emitterBuffer, SimBindings::Emitters);

    LoadInternalShaders();   // keygen + radix; collision loaded on demand

    InitializeLifetimeLUTs();
}

void ParticleWorld::InitializeDeadList()
{
    std::vector<uint32_t> idx(m_maxElements);
    for (uint32_t i = 0; i < m_maxElements; ++i) idx[i] = i;

    glCreateBuffers(1, &m_deadListBuffer);
    glNamedBufferStorage(m_deadListBuffer, m_maxElements * sizeof(uint32_t), idx.data(), 0);

    uint32_t initial = m_maxElements;
    glCreateBuffers(1, &m_deadCountBuffer);
    glNamedBufferStorage(m_deadCountBuffer, sizeof(uint32_t), &initial, 0);
}

void ParticleWorld::InitializeSortBuffers()
{
    std::vector<SortEntry> entries(m_maxElements);
    for (auto& e : entries) { e.particleIndex = 0; e.depthBits = 0xFFFFFFFFu; }

    glCreateBuffers(1, &m_sortBuffer);
    glNamedBufferStorage(m_sortBuffer, m_maxElements * sizeof(SortEntry), entries.data(), 0);

    glCreateBuffers(1, &m_sortBufferAlt);
    glNamedBufferStorage(m_sortBufferAlt, m_maxElements * sizeof(SortEntry), nullptr, 0);

    const uint32_t groups = m_maxElements / k_threadGroup;
    glCreateBuffers(1, &m_histogramBuffer);
    glNamedBufferStorage(m_histogramBuffer, k_radixBuckets * groups * sizeof(uint32_t), nullptr, 0);
}

void ParticleWorld::InitializeEmitterBuffer()
{
    glCreateBuffers(1, &m_emitterBuffer);
    glNamedBufferStorage(m_emitterBuffer, k_maxEmitters * sizeof(GpuEmitterData),
        nullptr, GL_DYNAMIC_STORAGE_BIT);
}

void ParticleWorld::InitializeUBOs()
{
    glCreateBuffers(1, &m_worldUBO);
    glNamedBufferStorage(m_worldUBO, sizeof(WorldParams), nullptr, GL_DYNAMIC_STORAGE_BIT);

    glCreateBuffers(1, &m_cameraUBO);
    glNamedBufferStorage(m_cameraUBO, sizeof(CameraParams), nullptr, GL_DYNAMIC_STORAGE_BIT);
}

void ParticleWorld::LoadInternalShaders()
{
    m_keygenShader = LoadComputeShader(m_shaderDir + "particle_sort_keygen.glsl");
    m_radixCountShader = LoadComputeShader(m_shaderDir + "particle_sort_radix_count.glsl");
    m_radixPrefixShader = LoadComputeShader(m_shaderDir + "particle_sort_radix_prefix.glsl");
    m_radixScatterShader = LoadComputeShader(m_shaderDir + "particle_sort_radix_scatter.glsl");
    m_collisionShader = LoadComputeShader(m_shaderDir + "particle_collision.glsl");
}

void ParticleWorld::SetupDefault(GLuint billboardShader)
{
    assert(m_initialized && "Initialize before SetupDefault");
    assert(billboardShader != 0);

    GLuint spawn = LoadComputeShader(m_shaderDir + "particle_spawn.glsl");
    GLuint update = LoadComputeShader(m_shaderDir + "particle_update.glsl");
    GLuint lifetime = LoadComputeShader(m_shaderDir + "particle_lifetime.glsl");
    GLuint indirect = LoadComputeShader(m_shaderDir + "particle_indirect.glsl");

    AddStage(std::make_unique<SpawnStage>(spawn));
    AddStage(std::make_unique<ComputeStage>(update));
    AddStage(std::make_unique<ComputeStage>(lifetime));
    AddStage(std::make_unique<SingleThreadStage>(indirect));

    AddRenderStage(std::make_unique<ParticleBillboardRenderStage>(billboardShader));
    // keygen + sort (+ collision) appended by Finalize on first Dispatch
}

void ParticleWorld::Finalize()
{
    AddStage(std::make_unique<ConditionalStage>(
        std::make_unique<TextureStage>(m_collisionShader, &m_depthTexture),
        [this]() { return m_collisionEnabled && m_depthTexture != 0; }
    ));


    // keygen writes current depths into the sort buffer, then radix sorts.
    AddStage(std::make_unique<ComputeStage>(m_keygenShader));
    AddStage(std::make_unique<SortStage>(m_radixCountShader, m_radixPrefixShader, m_radixScatterShader, m_maxElements));

    m_finalized = true;
}

void ParticleWorld::SetCamera(const glm::mat4& view, const glm::mat4& proj)
{
    CameraParams cam{};
    cam.view = view;
    cam.proj = proj;
    cam.invView = glm::inverse(view);
    cam.invProj = glm::inverse(proj);
    glNamedBufferSubData(m_cameraUBO, 0, sizeof(CameraParams), &cam);
}

ParticleEmitter* ParticleWorld::CreateEmitter()
{
    // recycle a freed slot, but only once its longest-lived particles are gone
    // (particles reference emitters by index, so reusing too early would alias them).
    for (size_t i = 0; i < m_freeSlots.size(); ++i)
    {
        if (m_freeSlots[i].reuseAfter <= m_totalTime)
        {
            uint32_t idx = m_freeSlots[i].index;
            m_freeSlots[i] = m_freeSlots.back();
            m_freeSlots.pop_back();

            ParticleEmitter* e = m_emitters[idx].get();
            *e = ParticleEmitter{};               // reset to defaults
            e->m_index = idx;
            return e;
        }
    }

    assert(m_emitters.size() < k_maxEmitters && "max emitters reached — none free to recycle yet");
    auto e = std::make_unique<ParticleEmitter>();
    e->m_index = static_cast<uint32_t>(m_emitters.size());
    ParticleEmitter* ptr = e.get();
    m_emitters.push_back(std::move(e));
    return ptr;
}

ParticleEmitter* ParticleWorld::CreateEmitter(const EmitterDesc& desc)
{
    ParticleEmitter* e = CreateEmitter();
    e->Configure(desc);
    return e;
}

void ParticleWorld::DestroyEmitter(ParticleEmitter* e)
{
    assert(e);
    e->SetActive(false);
    // free the slot for reuse once this emitter's particles have all expired
    m_freeSlots.push_back({ e->GetIndex(), m_totalTime + e->m_maxLifetime });
}

void ParticleWorld::RegisterWith(ParticleWorldRegistry& registry, const std::string& name)
{
    m_name = name;
    m_registry = &registry;
    registry.Register(this);
}

uint32_t ParticleWorld::GetActiveEmitterCount() const
{
    uint32_t count = 0;
    for (const auto& e : m_emitters)
        if (e->IsActive()) ++count;
    return count;
}

uint32_t ParticleWorld::ReadbackAliveCount() const
{
    if (!m_deadCountBuffer) return 0;
    uint32_t dead = 0;
    glGetNamedBufferSubData(m_deadCountBuffer, 0, sizeof(uint32_t), &dead);
    return (dead <= m_maxElements) ? (m_maxElements - dead) : 0;
}

uint32_t ParticleWorld::UploadEmitterData(float dt)
{
    std::vector<GpuEmitterData> data;
    data.reserve(m_emitters.size());

    uint32_t offset = 0;
    for (auto& e : m_emitters)
    {
        GpuEmitterData d = e->BuildGpuData(dt);

        d.spawnOffset = offset;     // prefix sum so spawn threads find their emitter
        offset += d.spawnCount;
        data.push_back(d);
    }

    if (!data.empty())
        glNamedBufferSubData(m_emitterBuffer, 0, data.size() * sizeof(GpuEmitterData), data.data());

    return offset;   // total spawns this frame
}

void ParticleWorld::UploadWorldParams(float dt)
{
    WorldParams p{};
    p.deltaTime = dt;
    p.time = m_totalTime;
    p.maxParticles = m_maxElements;
    p.emitterCount = static_cast<uint32_t>(m_emitters.size());
    glNamedBufferSubData(m_worldUBO, 0, sizeof(WorldParams), &p);
}

void ParticleWorld::BindUBOs()
{
    glBindBufferBase(GL_UNIFORM_BUFFER, SimBindings::WorldUBO, m_worldUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, SimBindings::CameraUBO, m_cameraUBO);
}

void ParticleWorld::Dispatch(float deltaTime)
{
    assert(m_initialized && "Dispatch before Initialize");
    if (!m_finalized) Finalize();   // append internal stages once

    m_totalTime += deltaTime;

    uint32_t spawnCount = UploadEmitterData(deltaTime);
    m_lastSpawnCount = spawnCount;
    UploadWorldParams(deltaTime);

    BindBuffers();
    BindUBOs();

    SimContext ctx{};
    ctx.deltaTime = deltaTime;
    ctx.totalTime = m_totalTime;
    ctx.maxElements = m_maxElements;
    ctx.emitterCount = static_cast<uint32_t>(m_emitters.size());
    ctx.spawnCount = spawnCount;

    RunStages(ctx);
}

GLuint ParticleWorld::CreateTextureArray(const std::vector<const uint8_t*>& layerPixels,
    uint32_t resolution)
{
    assert(!layerPixels.empty() && "CreateTextureArray: no layers provided");
    assert(resolution > 0);

    const uint32_t layers = static_cast<uint32_t>(layerPixels.size());

    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_textureArray);
    m_ownsTextureArray = true;

    // full mip chain for the chosen resolution
    uint32_t mips = 1;
    for (uint32_t r = resolution; r > 1; r >>= 1) ++mips;

    glTextureStorage3D(m_textureArray, mips, GL_RGBA8, resolution, resolution, layers);

    // upload each layer — caller guarantees resolution x resolution x 4 RGBA8 bytes
    for (uint32_t layer = 0; layer < layers; ++layer)
    {
        assert(layerPixels[layer] != nullptr && "CreateTextureArray: null layer data");
        glTextureSubImage3D(
            m_textureArray,
            0,                      // mip 0
            0, 0, layer,            // x, y, layer offset
            resolution, resolution, 1,
            GL_RGBA, GL_UNSIGNED_BYTE,
            layerPixels[layer]
        );
    }

    glGenerateTextureMipmap(m_textureArray);

    // sampling params — trilinear, clamp to avoid edge wrap on sprite frames
    glTextureParameteri(m_textureArray, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_textureArray, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_textureArray, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_textureArray, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return m_textureArray;
}

// ---- lifetime LUT (colour + size over life) -----------------------------
// EvalGradient / EvalCurve live in EmitterDesc.h (shared with the editor preview).

void ParticleWorld::InitializeLifetimeLUTs()
{
    glCreateTextures(GL_TEXTURE_2D, 1, &m_lutColour);
    glTextureStorage2D(m_lutColour, 1, GL_RGBA16F, k_lutWidth, k_maxEmitters);   // HDR: emissive colours > 1 allowed
    glTextureParameteri(m_lutColour, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_lutColour, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_lutColour, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_lutColour, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glCreateTextures(GL_TEXTURE_2D, 1, &m_lutScalar);
    glTextureStorage2D(m_lutScalar, 1, GL_RGBA16F, k_lutWidth, k_maxEmitters);   // R = size; GBA reserved for future scalar curves
    glTextureParameteri(m_lutScalar, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_lutScalar, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_lutScalar, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_lutScalar, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Re-evaluate each emitter's curves into its LUT row. Row index = emitter index,
// so the shaders sample the LUT by the particle's emitterIndex (no extra GPU field).
void ParticleWorld::BakeLifetimeLUTs()
{
    const uint32_t rows = static_cast<uint32_t>(m_emitters.size());
    if (rows == 0) return;

    // RGBA16F rows uploaded as float (GL converts to half). Colour allows >1 for
    // HDR/emissive; scalar packs size in R with GBA reserved for future curves.
    std::vector<glm::vec4> colour(size_t(k_lutWidth) * rows);
    std::vector<glm::vec4> scalar(size_t(k_lutWidth) * rows);

    for (uint32_t e = 0; e < rows; ++e)
    {
        const ParticleEmitter& em = *m_emitters[e];
        for (uint32_t i = 0; i < k_lutWidth; ++i)
        {
            float t = (k_lutWidth > 1) ? float(i) / float(k_lutWidth - 1) : 0.f;
            size_t idx = size_t(e) * k_lutWidth + i;
            colour[idx] = glm::max(EvalGradient(em.m_colourOverLife, em.m_colourStopCount, t), 0.f); // clamp negatives only
            scalar[idx] = glm::vec4(EvalCurve(em.m_sizeOverLife, em.m_sizeKeyCount, t), 0.f, 0.f, 0.f);
        }
    }

    glTextureSubImage2D(m_lutColour, 0, 0, 0, k_lutWidth, rows, GL_RGBA, GL_FLOAT, colour.data());
    glTextureSubImage2D(m_lutScalar, 0, 0, 0, k_lutWidth, rows, GL_RGBA, GL_FLOAT, scalar.data());
}

void ParticleWorld::Render()
{
    assert(!m_renderStages.empty() && "no render stages");

    BakeLifetimeLUTs();   // refresh colour/size-over-life rows (cheap; covers live edits)
    BindUBOs();

    RenderContext ctx{};
    ctx.primaryBuffer = m_primaryBuffer;
    ctx.indirectBuffer = m_indirectBuffer;
    ctx.sortBuffer = m_sortBuffer;
    ctx.emitterBuffer = m_emitterBuffer;
    ctx.emptyVAO = m_emptyVAO;
    ctx.cameraUBO = m_cameraUBO;
    ctx.depthTexture = m_depthTexture;
    ctx.nearPlane = m_near;
    ctx.farPlane = m_far;
    ctx.softFadeDist = m_softFade;
    ctx.textureArray = m_textureArray;
    ctx.lutColour = m_lutColour;
    ctx.lutScalar = m_lutScalar;

    RunRenderStages(ctx);
}