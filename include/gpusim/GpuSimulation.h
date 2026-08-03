#pragma once
#include "SimContext.h"
#include "SimStage.h"
#include "RenderStage.h"
#include "ParticleTypes.h"
#include "GpuProfiler.h"
#include <glad/gl.h>
#include <vector>
#include <memory>
#include <string>
#include <cstdint>

class GpuSimulation
{
public:
    GpuSimulation() = default;
    virtual ~GpuSimulation();

    GpuSimulation(const GpuSimulation&) = delete;
    GpuSimulation& operator=(const GpuSimulation&) = delete;
    GpuSimulation(GpuSimulation&&) noexcept;
    GpuSimulation& operator=(GpuSimulation&&) noexcept;

    virtual void Initialize(uint32_t elementCount, uint32_t elementStride);

    void AddStage(std::unique_ptr<SimStage> stage);
    void AddRenderStage(std::unique_ptr<RenderStage> stage);
    void AddAuxBuffer(GLuint buffer, uint32_t bindingPoint);

    virtual void Dispatch(float deltaTime);
    virtual void Render();

    GLuint   GetPrimaryBuffer()  const { return m_primaryBuffer; }
    GLuint   GetIndirectBuffer() const { return m_indirectBuffer; }
    GLuint   GetEmptyVAO()       const { return m_emptyVAO; }
    uint32_t GetMaxElements()    const { return m_maxElements; }
    bool     IsInitialized()     const { return m_initialized; }

    // per-stage GPU times (non-blocking; values are a few frames old)
    GpuProfiler&       Profiler()       { return m_profiler; }
    const GpuProfiler& Profiler() const { return m_profiler; }

protected:
    void BindBuffers();
    void RunStages(const SimContext& ctx);
    void RunRenderStages(const RenderContext& ctx);

    void AllocatePrimaryBuffer(uint32_t byteSize, const void* data = nullptr);
    void AllocateIndirectBuffer();
    void AllocateEmptyVAO();

    static GLuint LoadComputeShader(const std::string& path);

    struct AuxBuffer { GLuint buffer; uint32_t bindingPoint; };

    GLuint   m_primaryBuffer = 0;
    GLuint   m_indirectBuffer = 0;
    GLuint   m_emptyVAO = 0;
    uint32_t m_maxElements = 0;
    uint32_t m_elementStride = 0;

    std::vector<std::unique_ptr<SimStage>>    m_stages;
    std::vector<std::unique_ptr<RenderStage>> m_renderStages;
    std::vector<AuxBuffer>                    m_auxBuffers;

    GpuProfiler m_profiler;

    bool m_initialized = false;
};