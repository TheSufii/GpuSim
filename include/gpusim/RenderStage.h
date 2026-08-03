#pragma once
#include "SimBindings.h"
#include <glad/gl.h>
#include <cassert>

struct RenderContext
{
    GLuint primaryBuffer = 0;
    GLuint indirectBuffer = 0;
    GLuint sortBuffer = 0;
    GLuint emitterBuffer = 0;
    GLuint emptyVAO = 0;

    GLuint cameraUBO = 0;

    GLuint depthTexture = 0;     // scene depth, for soft particles (optional)
    float  nearPlane = 0.1f;
    float  farPlane = 1000.f;
    float  softFadeDist = 0.5f;

    GLuint textureArray = 0;     // GL_TEXTURE_2D_ARRAY of particle textures (optional)

    GLuint lutColour = 0;        // lifetime colour/alpha LUT atlas (sampler2D, optional)
    GLuint lutScalar = 0;        // lifetime scalar/size LUT atlas (sampler2D, optional)
};


// RenderStage — base for all particle render passes.

class RenderStage
{
public:
    virtual ~RenderStage() = default;
    virtual void Render(const RenderContext& ctx) = 0;
};

// Single pass, premultiplied alpha. Additive and alpha-blended particles
// composite correctly in one depth-sorted draw because the blend func is
// unified: glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA). The fragment shader
// decides per-particle output alpha from its emitter's blendMode.
// Camera matrices come from the CameraUBO the library binds — not from here.
// Self-contained GL state so it never depends on whatever the host left bound.

class ParticleBillboardRenderStage : public RenderStage
{
public:
    explicit ParticleBillboardRenderStage(GLuint shader) : m_shader(shader)
    {
        assert(shader != 0 && "ParticleBillboardRenderStage: invalid shader");
        // cache uniform locations once — they never change for a linked program
        m_uNear            = glGetUniformLocation(shader, "u_near");
        m_uFar             = glGetUniformLocation(shader, "u_far");
        m_uSoftFadeDist    = glGetUniformLocation(shader, "u_softFadeDist");
        m_uSoftEnabled     = glGetUniformLocation(shader, "u_softEnabled");
        m_uHasTextureArray = glGetUniformLocation(shader, "u_hasTextureArray");
    }

    void Render(const RenderContext& ctx) override
    {
   
        glUseProgram(m_shader);
        glBindVertexArray(ctx.emptyVAO);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SimBindings::Particles, ctx.primaryBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SimBindings::SortKeys, ctx.sortBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SimBindings::Emitters, ctx.emitterBuffer);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, ctx.indirectBuffer);

        // soft particles: bind scene depth + linearization params
        glBindTextureUnit(SimBindings::DepthTextureUnit, ctx.depthTexture);
        if (m_uNear         != -1) glUniform1f(m_uNear, ctx.nearPlane);
        if (m_uFar          != -1) glUniform1f(m_uFar, ctx.farPlane);
        if (m_uSoftFadeDist != -1) glUniform1f(m_uSoftFadeDist, ctx.softFadeDist);
        if (m_uSoftEnabled  != -1) glUniform1ui(m_uSoftEnabled, ctx.depthTexture != 0 ? 1u : 0u);

        // particle texture array (sampler2DArray). if absent, shader uses procedural circle.
        glBindTextureUnit(SimBindings::ParticleTextureUnit, ctx.textureArray);
        if (m_uHasTextureArray != -1) glUniform1ui(m_uHasTextureArray, ctx.textureArray != 0 ? 1u : 0u);

        // lifetime LUT atlases (colour/alpha sampled in frag, size in vert)
        glBindTextureUnit(SimBindings::LutColourUnit, ctx.lutColour);
        glBindTextureUnit(SimBindings::LutScalarUnit, ctx.lutScalar);

        // state this pass requires — set explicitly, restore sane defaults after
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);    // premultiplied alpha
        glEnable(GL_DEPTH_TEST);                        // occluded by opaque geometry
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);                          // but do not write depth
        glDisable(GL_CULL_FACE);                        // billboards are double-sided

        glDrawArraysIndirect(GL_TRIANGLES, nullptr);

        // restore to common defaults; host sets its own state for later passes
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glBindVertexArray(0);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    }

private:
    GLuint m_shader;
    GLint  m_uNear = -1, m_uFar = -1, m_uSoftFadeDist = -1, m_uSoftEnabled = -1, m_uHasTextureArray = -1;
};