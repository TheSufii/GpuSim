#pragma once
#include "SimContext.h"
#include "SimBindings.h"
#include <glad/glad.h>
#include <functional>
#include <memory>
#include <cassert>
#include <cstdint>

class SimStage {

public:
	virtual ~SimStage() = default;

	virtual void Execute(const SimContext& ctx) = 0;

    // barrier issued after Execute — override for non-default needs
    virtual GLbitfield GetBarrierBits() const { return GL_SHADER_STORAGE_BARRIER_BIT; }

protected:
    static constexpr uint32_t k_groupSize = 256;

    static uint32_t GroupsFor(uint32_t threads)
    {
        return (threads + k_groupSize - 1) / k_groupSize;
    }
};

// --------------------------------------------------------
// ComputeStage — one thread per element. update, lifetime, keygen, custom.
// --------------------------------------------------------
class ComputeStage : public SimStage
{
public:
    explicit ComputeStage(GLuint shader) : m_shader(shader)
    {
        assert(shader != 0 && "ComputeStage: invalid shader");
    }

    void Execute(const SimContext& ctx) override
    {
        glUseProgram(m_shader);
        glDispatchCompute(GroupsFor(ctx.maxElements), 1, 1);
    }

protected:
    GLuint m_shader;
};

// --------------------------------------------------------
// SpawnStage — one thread per SPAWN (not per emitter).
// Dispatches ctx.spawnCount threads; each creates one particle.
// --------------------------------------------------------
class SpawnStage : public SimStage
{
public:
    explicit SpawnStage(GLuint shader) : m_shader(shader)
    {
        assert(shader != 0 && "SpawnStage: invalid shader");
    }

    void Execute(const SimContext& ctx) override
    {
        if (ctx.spawnCount == 0) return;            // nothing to spawn this frame
        glUseProgram(m_shader);


        GLint l = glGetUniformLocation(m_shader, "u_spawnCount");
        if (l != -1) glUniform1ui(l, ctx.spawnCount);

        glDispatchCompute(GroupsFor(ctx.spawnCount), 1, 1);
    }

private:
    GLuint m_shader;
};

// --------------------------------------------------------
// SingleThreadStage — one thread. indirect write.
// --------------------------------------------------------
class SingleThreadStage : public SimStage
{
public:
    explicit SingleThreadStage(GLuint shader) : m_shader(shader)
    {
        assert(shader != 0 && "SingleThreadStage: invalid shader");
    }

    void Execute(const SimContext& ctx) override
    {
        glUseProgram(m_shader);
        glDispatchCompute(1, 1, 1);
    }

    GLbitfield GetBarrierBits() const override { return GL_COMMAND_BARRIER_BIT; }

private:
    GLuint m_shader;
};

// --------------------------------------------------------
// TextureStage — one thread per element, binds a texture first.
// Used for depth-buffer collision. depthTextureRef is a pointer so it
// always reflects the latest SetDepthTexture each frame.
// --------------------------------------------------------
class TextureStage : public ComputeStage
{
public:
    TextureStage(GLuint shader, const GLuint* depthTextureRef)
        : ComputeStage(shader), m_depthTextureRef(depthTextureRef)
    {
        assert(depthTextureRef != nullptr && "TextureStage: null depth texture ref");
    }

    void Execute(const SimContext& ctx) override
    {
        glBindTextureUnit(SimBindings::DepthTextureUnit, *m_depthTextureRef);
        ComputeStage::Execute(ctx);
    }

private:
    const GLuint* m_depthTextureRef;
};

// --------------------------------------------------------
// ConditionalStage — wraps a stage, runs only when condition() is true.
// Evaluated every frame so runtime toggles take effect immediately.
// --------------------------------------------------------
class ConditionalStage : public SimStage
{
public:
    ConditionalStage(std::unique_ptr<SimStage> inner, std::function<bool()> condition)
        : m_inner(std::move(inner)), m_condition(std::move(condition))
    {
        assert(m_inner && m_condition && "ConditionalStage: null inner or condition");
    }

    void Execute(const SimContext& ctx) override
    {
        if (m_condition()) m_inner->Execute(ctx);
    }

    GLbitfield GetBarrierBits() const override { return m_inner->GetBarrierBits(); }

private:
    std::unique_ptr<SimStage> m_inner;
    std::function<bool()>     m_condition;
};