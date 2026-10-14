#pragma once
#include "ParticleTypes.h"
#include "EmitterDesc.h"
#include <glm/glm.hpp>
#include <cstdint>

class ParticleWorld;

class ParticleEmitter
{
public:
    ParticleEmitter() = default;

    // designer facing — local space, stable
    void SetLocalOffset(const glm::vec3& v);
    void SetLocalDirection(const glm::vec3& v);
    void SetSpread(float radians);
    void SetSpawnRate(float perSecond);
    void SetLifetimeRange(float lo, float hi);
    void SetSizeRange(float lo, float hi);
    void SetSpeedRange(float lo, float hi);
    void SetDrag(float drag);
    void SetNoiseStrength(float s);
    void SetNoiseScale(float s);
    void SetNoiseScrollSpeed(float s);
    void SetColour(const glm::vec4& c);
    void SetIntensity(float i);                            // multiplies colour.rgb
    void SetColourVariance(const glm::vec3& v);            // per-channel random ± at spawn
    void SetBlendMode(BlendMode m);
    void SetAttractor(const glm::vec3& worldPos, float strength);
    void SetUseAttractor(bool b);
    void SetUseCollision(bool b);
    void SetRestitution(float r);
    void SetCollisionThickness(float t);                   // depth-gate distance
    void SetCollisionSeparation(float s);                  // push-out along surface normal
    void SetActive(bool b);
    void SetTexture(uint32_t layer);                       // array layer; or kNoTexture
    void SetFrameCount(uint32_t count);                    // contiguous layers; 1 = static
    void SetAnimMode(AnimMode mode);
    void SetAnimFps(float fps);                            // used only by AnimMode::Loop

    // one-shot burst — adds count to this frame's spawn, then clears.
    // particles reference this persistent emitter, so their params stay valid.
    void Burst(uint32_t count) { m_burstCount += count; }

    // ECS facing — world space, resolved each frame
    void SetWorldPosition(const glm::vec3& v);
    void SetWorldDirection(const glm::vec3& v);
    void SetWorldTangent(const glm::vec3& v);              // emission shape's +X; zero = derived from direction
    void SetGravity(const glm::vec3& worldGravity);

    glm::vec3 GetLocalOffset()    const { return m_localOffset; }
    glm::vec3 GetLocalDirection() const { return m_localDirection; }
    bool      IsActive()          const { return m_active; }
    uint32_t  GetIndex()          const { return m_index; }
    BlendMode GetBlendMode()      const { return m_blendMode; }

    // builds GPU struct; accumulates continuous spawns + burst.
    // spawnOffset is filled by ParticleWorld after this returns.
    GpuEmitterData BuildGpuData(float deltaTime);

    // apply all template fields from a desc (placement/world fields excluded)
    void Configure(const EmitterDesc& desc);
    // snapshot current template fields into a desc (for save / inspector / tooling)
    EmitterDesc ToDesc() const;

private:
    friend class ParticleWorld;

    glm::vec3 m_localOffset = glm::vec3(0.f);
    glm::vec3 m_localDirection = glm::vec3(0, 1, 0);
    glm::vec3 m_worldPosition = glm::vec3(0.f);
    glm::vec3 m_worldDirection = glm::vec3(0, 1, 0);
    glm::vec3 m_worldTangent = glm::vec3(0.f);
    glm::vec3 m_gravity = glm::vec3(0, -9.8f, 0);

    float    m_spawnRate = 100.f;
    float    m_spawnAccumulator = 0.f;
    uint32_t m_burstCount = 0;
    float    m_spread = 0.3f;
    float    m_minSpeed = 1.f;
    float    m_maxSpeed = 3.f;
    float    m_minLifetime = 1.f;
    float    m_maxLifetime = 2.f;
    float    m_minSize = 0.05f;
    float    m_maxSize = 0.1f;
    float    m_drag = 0.f;
    float    m_noiseStrength = 0.f;
    float    m_noiseScale = 1.f;
    float    m_noiseScrollSpeed = 0.f;

    glm::vec3 m_attractorPos = glm::vec3(0.f);
    float     m_attractorStrength = 0.f;
    bool      m_useAttractor = false;

    glm::vec4 m_colour = glm::vec4(1.f);
    float     m_intensity = 1.f;
    glm::vec3 m_colourVariance = glm::vec3(0.f);
    BlendMode m_blendMode = BlendMode::AlphaBlend;

    bool  m_useCollision = false;
    float m_restitution = 0.3f;
    float m_collisionThickness = 0.75f;
    float m_collisionSeparation = 0.02f;

    bool     m_active = true;
    uint32_t m_index = 0;

    uint32_t m_textureIndex = kNoTexture;
    uint32_t m_frameCount = 1;
    AnimMode m_animMode = AnimMode::PlayOnce;
    float    m_animFps = 12.f;
    float    m_velocityStretch = 0.f;

    EmissionMode m_emissionMode = EmissionMode::Continuous;
    uint32_t     m_burstAmount = 32;     // particles per burst (desc.burstCount)
    float        m_burstInterval = 0.f;
    float        m_burstTimer = 0.f;
    bool         m_burstStarted = false;

    // emission timeline (CPU-gated; gates spawnCount, no GPU field needed)
    float m_startDelay = 0.f;            // seconds before this emitter starts
    float m_duration   = 0.f;            // seconds it emits (<= 0 = forever)
    float m_age        = 0.f;            // seconds since (re)creation; runtime

    EmissionShape m_emissionShape = EmissionShape::Point;
    glm::vec3     m_emissionShapeSize = glm::vec3(0.5f);
    float         m_shapeDirection = 0.f;

    // lifetime curves (read by ParticleWorld to bake the LUT)
    GradientStop m_colourOverLife[MAX_GRADIENT_STOPS];
    uint32_t     m_colourStopCount = 2;
    CurveKey     m_sizeOverLife[MAX_CURVE_KEYS];
    uint32_t     m_sizeKeyCount = 2;

    // set when curve data changes; cleared by ParticleWorld::BakeLifetimeLUTs
    bool m_lutDirty = true;
};