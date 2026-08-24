#include "GpuSimulation.h"
#include "SimBindings.h"
#include "ShaderPreprocessor.h"
#include <cassert>
#include <cstdio>
#include <utility>

GpuSimulation::~GpuSimulation()
{
    if (m_primaryBuffer)  glDeleteBuffers(1, &m_primaryBuffer);
    if (m_indirectBuffer) glDeleteBuffers(1, &m_indirectBuffer);
    if (m_emptyVAO)       glDeleteVertexArrays(1, &m_emptyVAO);
}

GpuSimulation::GpuSimulation(GpuSimulation&& o) noexcept
    : m_primaryBuffer(std::exchange(o.m_primaryBuffer, 0))
    , m_indirectBuffer(std::exchange(o.m_indirectBuffer, 0))
    , m_emptyVAO(std::exchange(o.m_emptyVAO, 0))
    , m_maxElements(std::exchange(o.m_maxElements, 0))
    , m_elementStride(std::exchange(o.m_elementStride, 0))
    , m_stages(std::move(o.m_stages))
    , m_renderStages(std::move(o.m_renderStages))
    , m_auxBuffers(std::move(o.m_auxBuffers))
    , m_profiler(std::move(o.m_profiler))
    , m_initialized(std::exchange(o.m_initialized, false))
{
}

GpuSimulation& GpuSimulation::operator=(GpuSimulation&& o) noexcept
{
    if (this != &o)
    {
        if (m_primaryBuffer)  glDeleteBuffers(1, &m_primaryBuffer);
        if (m_indirectBuffer) glDeleteBuffers(1, &m_indirectBuffer);
        if (m_emptyVAO)       glDeleteVertexArrays(1, &m_emptyVAO);

        m_primaryBuffer = std::exchange(o.m_primaryBuffer, 0);
        m_indirectBuffer = std::exchange(o.m_indirectBuffer, 0);
        m_emptyVAO = std::exchange(o.m_emptyVAO, 0);
        m_maxElements = std::exchange(o.m_maxElements, 0);
        m_elementStride = std::exchange(o.m_elementStride, 0);
        m_stages = std::move(o.m_stages);
        m_renderStages = std::move(o.m_renderStages);
        m_auxBuffers = std::move(o.m_auxBuffers);
        m_profiler = std::move(o.m_profiler);
        m_initialized = std::exchange(o.m_initialized, false);
    }
    return *this;
}

void GpuSimulation::Initialize(uint32_t elementCount, uint32_t elementStride)
{
    assert(!m_initialized && "already initialized");
    assert(elementCount > 0 && elementStride > 0);

    m_maxElements = elementCount;
    m_elementStride = elementStride;

    AllocatePrimaryBuffer(elementCount * elementStride);
    AllocateIndirectBuffer();
    AllocateEmptyVAO();

    m_initialized = true;
}

void GpuSimulation::AllocatePrimaryBuffer(uint32_t byteSize, const void* data)
{
    glCreateBuffers(1, &m_primaryBuffer);
    glNamedBufferStorage(m_primaryBuffer, byteSize, data, GL_DYNAMIC_STORAGE_BIT);
}

void GpuSimulation::AllocateIndirectBuffer()
{
    DrawArraysIndirectCommand cmd{ 0, 1, 0, 0 };
    glCreateBuffers(1, &m_indirectBuffer);
    glNamedBufferStorage(m_indirectBuffer, sizeof(cmd), &cmd, GL_DYNAMIC_STORAGE_BIT);
}

void GpuSimulation::AllocateEmptyVAO()
{
    // GL requires a bound VAO for draws; billboards use gl_VertexID only.
    glCreateVertexArrays(1, &m_emptyVAO);
}

void GpuSimulation::AddStage(std::unique_ptr<SimStage> stage)
{
    assert(stage && "AddStage: null stage");
    m_stages.push_back(std::move(stage));
}

void GpuSimulation::AddRenderStage(std::unique_ptr<RenderStage> stage)
{
    assert(stage && "AddRenderStage: null stage");
    m_renderStages.push_back(std::move(stage));
}

void GpuSimulation::AddAuxBuffer(GLuint buffer, uint32_t bindingPoint)
{
    assert(buffer != 0 && "AddAuxBuffer: invalid buffer");
    for (const auto& a : m_auxBuffers)
        assert(a.bindingPoint != bindingPoint && "AddAuxBuffer: binding point already in use");
    m_auxBuffers.push_back({ buffer, bindingPoint });
}

void GpuSimulation::BindBuffers()
{
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SimBindings::Particles, m_primaryBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SimBindings::Indirect, m_indirectBuffer);
    for (const auto& a : m_auxBuffers)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, a.bindingPoint, a.buffer);
}

void GpuSimulation::RunStages(const SimContext& ctx)
{
    for (auto& stage : m_stages)
    {
        m_profiler.Begin(stage->GetDebugName());
        stage->Execute(ctx);
        GLbitfield bits = stage->GetBarrierBits();
        if (bits) glMemoryBarrier(bits);
        m_profiler.End();
    }
}

void GpuSimulation::RunRenderStages(const RenderContext& ctx)
{
    m_profiler.Begin("Render");
    for (auto& stage : m_renderStages) stage->Render(ctx);
    m_profiler.End();
}

void GpuSimulation::Dispatch(float deltaTime)
{
    assert(m_initialized && !m_stages.empty());
    m_profiler.BeginFrame();
    BindBuffers();
    SimContext ctx{ deltaTime, 0.f, m_maxElements, 0, 0 };
    RunStages(ctx);
}

void GpuSimulation::Render()
{
    assert(!m_renderStages.empty());
    RenderContext ctx{};
    ctx.primaryBuffer = m_primaryBuffer;
    ctx.indirectBuffer = m_indirectBuffer;
    ctx.emptyVAO = m_emptyVAO;
    RunRenderStages(ctx);
}

GLuint GpuSimulation::LoadComputeShader(const std::string& path)
{

    // NDEBUG strips the asserts below, so every failure path also has to bail
    // out on its own — otherwise a release build feeds an empty source string
    // to the driver and carries on with a garbage program object.
    GPUSim::PreprocessResult pp = GPUSim::PreprocessShader(path);
    if (!pp.success) {
        std::fprintf(stderr, "GpuSimulation: %s\n", pp.error.c_str());
        assert(false && "compute shader preprocess failed");
        return 0;
    }

    const char* src = pp.source.c_str();

    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(shader, 1024, nullptr, log);
               std::fprintf(stderr, "GpuSimulation: compute compile failed (%s):\n%s\n", path.c_str(), log);
               assert(false && "compute shader compile failed");
               glDeleteShader(shader); return 0; }

    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    glDeleteShader(shader);
    if (!ok) { char log[1024]; glGetProgramInfoLog(program, 1024, nullptr, log);
               std::fprintf(stderr, "GpuSimulation: compute link failed (%s):\n%s\n", path.c_str(), log);
               assert(false && "compute shader link failed");
               glDeleteProgram(program); return 0; }

    return program;
}