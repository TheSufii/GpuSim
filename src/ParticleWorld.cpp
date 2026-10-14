#include "ParticleWorld.h"
#include "ParticleWorldRegistry.h"
#include "SimBindings.h"
#include <cassert>
#include <vector>
  


// internal radix SortStage — always runs. owns the three radix programs.
// Group counts come from a GPU-written dispatch-indirect buffer, so the sort
// scales with the alive count instead of buffer capacity.

class SortStage : public SimStage
{
public:
    SortStage(GLuint count, GLuint prefix, GLuint scatter, GLuint dispatchBuffer)
        : m_count(count), m_prefix(prefix), m_scatter(scatter), m_dispatchBuffer(dispatchBuffer)
    {
        assert(count && prefix && scatter && "SortStage: invalid shaders");
        assert(dispatchBuffer != 0 && "SortStage: invalid dispatch buffer");
        m_countShift   = glGetUniformLocation(count,   "u_bitShift");
        m_countPing    = glGetUniformLocation(count,   "u_pingPong");
        m_scatterShift = glGetUniformLocation(scatter, "u_bitShift");
        m_scatterPing  = glGetUniformLocation(scatter, "u_pingPong");
    }

    void Execute(const SimContext& ctx)
    {
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, m_dispatchBuffer);
        for (uint32_t pass = 0; pass < 4; ++pass)
        {
            uint32_t shift = pass * 8;
            uint32_t pingPong = pass & 1;

            glUseProgram(m_count);
            glUniform1ui(m_countShift, shift);
            glUniform1ui(m_countPing, pingPong);
            glDispatchComputeIndirect(0);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            glUseProgram(m_prefix);
            glDispatchCompute(1, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            glUseProgram(m_scatter);
            glUniform1ui(m_scatterShift, shift);
            glUniform1ui(m_scatterPing, pingPong);
            glDispatchComputeIndirect(0);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    }

    // last stage before the draw: vertex shader reads SSBOs, indirect is a draw
    // command, and the frag samples the depth texture for soft particles.
    GLbitfield GetBarrierBits() const override
    {
        return GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT;
    }

private:
    GLuint m_count, m_prefix, m_scatter;
    GLuint m_dispatchBuffer;
    GLint  m_countShift = -1, m_countPing = -1, m_scatterShift = -1, m_scatterPing = -1;
};


ParticleWorld::~ParticleWorld()
{
    if (m_registry) m_registry->Unregister(this);

    GLuint bufs[] = { m_deadListBuffer, m_deadCountBuffer, m_sortBuffer,
                      m_sortBufferAlt, m_histogramBuffer, m_sortCountBuffer,
                      m_sortDispatchBuffer, m_emitterBuffer,
                      m_worldUBO, m_cameraUBO };
    for (GLuint b : bufs) if (b) glDeleteBuffers(1, &b);

    if (m_aliveFence) glDeleteSync(m_aliveFence);
    if (m_aliveReadback)
    {
        glUnmapNamedBuffer(m_aliveReadback);
        glDeleteBuffers(1, &m_aliveReadback);
    }

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

    // shader paths are built by concatenation below, so tolerate a caller that
    // passes the directory without a trailing separator
    m_shaderDir = shaderDir;
    if (!m_shaderDir.empty() && m_shaderDir.back() != '/' && m_shaderDir.back() != '\\')
        m_shaderDir += '/';

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
    AddAuxBuffer(m_sortCountBuffer, SimBindings::SortCount);
    AddAuxBuffer(m_sortDispatchBuffer, SimBindings::SortDispatch);
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

    // alive-entry counter, compacted into by keygen (zeroed each Dispatch)
    const uint32_t zero = 0;
    glCreateBuffers(1, &m_sortCountBuffer);
    glNamedBufferStorage(m_sortCountBuffer, sizeof(uint32_t), &zero, 0);

    // dispatch-indirect args {groups, 1, 1} for the radix passes, GPU-written
    const uint32_t initialDispatch[3] = { 0, 1, 1 };
    glCreateBuffers(1, &m_sortDispatchBuffer);
    glNamedBufferStorage(m_sortDispatchBuffer, sizeof(initialDispatch), initialDispatch, 0);
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
    m_indirectShader = LoadComputeShader(m_shaderDir + "particle_indirect.glsl");
    m_collisionShader = LoadComputeShader(m_shaderDir + "particle_collision.glsl");
}

void ParticleWorld::SetupDefault(GLuint billboardShader)
{
    assert(m_initialized && "Initialize before SetupDefault");
    assert(billboardShader != 0);

    GLuint spawn = LoadComputeShader(m_shaderDir + "particle_spawn.glsl");
    GLuint update = LoadComputeShader(m_shaderDir + "particle_update.glsl");
    GLuint lifetime = LoadComputeShader(m_shaderDir + "particle_lifetime.glsl");

    auto AddNamed = [this](std::unique_ptr<SimStage> stage, const char* name)
    { stage->SetDebugName(name); AddStage(std::move(stage)); };

    AddNamed(std::make_unique<SpawnStage>(spawn), "Spawn");
    AddNamed(std::make_unique<ComputeStage>(update), "Update");
    AddNamed(std::make_unique<ComputeStage>(lifetime), "Lifetime");

    AddRenderStage(std::make_unique<ParticleBillboardRenderStage>(billboardShader));
    // collision + keygen + indirect args + sort appended by Finalize on first Dispatch
}

void ParticleWorld::Finalize()
{
    auto AddNamed = [this](std::unique_ptr<SimStage> stage, const char* name)
    { stage->SetDebugName(name); AddStage(std::move(stage)); };

    // collision runs before keygen so killed particles never enter the sort
    // (and the draw count, derived from the compacted entries, stays exact).
    AddNamed(std::make_unique<ConditionalStage>(
        std::make_unique<TextureStage>(m_collisionShader, &m_depthTexture),
        [this]() { return m_collisionEnabled && m_depthTexture != 0; }
    ), "Collision");

    // keygen compacts alive entries + their depths into the sort buffer;
    // the indirect stage turns the compacted count into draw + dispatch args;
    // the radix passes then sort only what is alive.
    AddNamed(std::make_unique<ComputeStage>(m_keygenShader), "Sort keygen");
    AddNamed(std::make_unique<SingleThreadStage>(m_indirectShader), "Indirect args");
    AddNamed(std::make_unique<SortStage>(m_radixCountShader, m_radixPrefixShader,
                                         m_radixScatterShader, m_sortDispatchBuffer), "Radix sort");

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

    if (!m_aliveReadback)
    {
        glCreateBuffers(1, &m_aliveReadback);
        glNamedBufferStorage(m_aliveReadback, sizeof(uint32_t), nullptr,
            GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_CLIENT_STORAGE_BIT);
        m_aliveMapped = static_cast<const uint32_t*>(glMapNamedBufferRange(
            m_aliveReadback, 0, sizeof(uint32_t),
            GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT));
    }

    // harvest the previous copy if the GPU has finished it (zero timeout = no stall)
    if (m_aliveFence)
    {
        GLenum r = glClientWaitSync(m_aliveFence, 0, 0);
        if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED)
        {
            m_cachedDeadCount = *m_aliveMapped;
            glDeleteSync(m_aliveFence);
            m_aliveFence = 0;
        }
    }

    // kick off the next copy once the previous one has landed
    if (!m_aliveFence)
    {
        glCopyNamedBufferSubData(m_deadCountBuffer, m_aliveReadback, 0, 0, sizeof(uint32_t));
        m_aliveFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }

    return (m_cachedDeadCount <= m_maxElements) ? (m_maxElements - m_cachedDeadCount) : 0;
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

    m_profiler.BeginFrame();
    m_totalTime += deltaTime;

    uint32_t spawnCount = UploadEmitterData(deltaTime);
    m_lastSpawnCount = spawnCount;
    UploadWorldParams(deltaTime);

    // keygen re-compacts alive entries from scratch each frame
    const uint32_t zero = 0;
    glClearNamedBufferSubData(m_sortCountBuffer, GL_R32UI, 0, sizeof(uint32_t),
                              GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);

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

    // free a previously-owned array first (rebuilds while authoring would otherwise
    // leak the old GL texture). never deletes an externally-set array we don't own.
    if (m_ownsTextureArray && m_textureArray)
        glDeleteTextures(1, &m_textureArray);

    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_textureArray);
    m_ownsTextureArray = true;

    // full mip chain for the chosen resolution
    uint32_t mips = 1;
    for (uint32_t r = resolution; r > 1; r >>= 1) ++mips;

    // sRGB: sprites are colour, so the GPU converts them to linear when sampling
    glTextureStorage3D(m_textureArray, mips, GL_SRGB8_ALPHA8, resolution, resolution, layers);

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
// so the shaders sample the LUT by the particle's emitterIndex.
// Only rows whose curves changed since the last bake are evaluated and uploaded
void ParticleWorld::BakeLifetimeLUTs()
{
    const uint32_t rows = static_cast<uint32_t>(m_emitters.size());
    if (rows == 0) return;

    // RGBA16F rows uploaded as float (GL converts to half). Colour allows >1 for
    // HDR/emissive; scalar packs size in R with GBA reserved for future curves.
    std::vector<glm::vec4> colour(k_lutWidth);
    std::vector<glm::vec4> scalar(k_lutWidth);

    for (uint32_t e = 0; e < rows; ++e)
    {
        ParticleEmitter& em = *m_emitters[e];
        if (!em.m_lutDirty) continue;
        em.m_lutDirty = false;

        for (uint32_t i = 0; i < k_lutWidth; ++i)
        {
            float t = (k_lutWidth > 1) ? float(i) / float(k_lutWidth - 1) : 0.f;
            colour[i] = glm::max(EvalGradient(em.m_colourOverLife, em.m_colourStopCount, t), 0.f); // clamp negatives only
            scalar[i] = glm::vec4(EvalCurve(em.m_sizeOverLife, em.m_sizeKeyCount, t), 0.f, 0.f, 0.f);
        }

        glTextureSubImage2D(m_lutColour, 0, 0, e, k_lutWidth, 1, GL_RGBA, GL_FLOAT, colour.data());
        glTextureSubImage2D(m_lutScalar, 0, 0, e, k_lutWidth, 1, GL_RGBA, GL_FLOAT, scalar.data());
    }
}

void ParticleWorld::Render()
{
    assert(!m_renderStages.empty() && "no render stages");

    BakeLifetimeLUTs();   // re-uploads only rows whose curves changed (covers live edits)
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