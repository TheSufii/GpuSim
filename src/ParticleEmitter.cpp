#include "ParticleEmitter.h"

void ParticleEmitter::SetLocalOffset(const glm::vec3& v) { m_localOffset = v; }
void ParticleEmitter::SetLocalDirection(const glm::vec3& v) { m_localDirection = v; }
void ParticleEmitter::SetSpread(float r) { m_spread = r; }
void ParticleEmitter::SetSpawnRate(float r) { m_spawnRate = r; }
void ParticleEmitter::SetDrag(float d) { m_drag = d; }
void ParticleEmitter::SetNoiseStrength(float s) { m_noiseStrength = s; }
void ParticleEmitter::SetNoiseScale(float s) { m_noiseScale = s; }
void ParticleEmitter::SetNoiseScrollSpeed(float s) { m_noiseScrollSpeed = s; }
void ParticleEmitter::SetColour(const glm::vec4& c) { m_colour = c; }
void ParticleEmitter::SetBlendMode(BlendMode m) { m_blendMode = m; }
void ParticleEmitter::SetUseAttractor(bool b) { m_useAttractor = b; }
void ParticleEmitter::SetUseCollision(bool b) { m_useCollision = b; }
void ParticleEmitter::SetRestitution(float r) { m_restitution = r; }
void ParticleEmitter::SetActive(bool b) { m_active = b; }
void ParticleEmitter::SetWorldPosition(const glm::vec3& v) { m_worldPosition = v; }
void ParticleEmitter::SetWorldDirection(const glm::vec3& v) { m_worldDirection = v; }
void ParticleEmitter::SetGravity(const glm::vec3& g) { m_gravity = g; }

void ParticleEmitter::SetLifetimeRange(float lo, float hi) { m_minLifetime = lo; m_maxLifetime = hi; }
void ParticleEmitter::SetSizeRange(float lo, float hi) { m_minSize = lo; m_maxSize = hi; }
void ParticleEmitter::SetSpeedRange(float lo, float hi) { m_minSpeed = lo; m_maxSpeed = hi; }
void ParticleEmitter::SetAttractor(const glm::vec3& p, float s) { m_attractorPos = p; m_attractorStrength = s; }

void ParticleEmitter::SetTexture(uint32_t layer) { m_textureIndex = layer; }
void ParticleEmitter::SetAnimMode(AnimMode mode) { m_animMode = mode; }
void ParticleEmitter::SetAnimFps(float fps) { m_animFps = fps; }
void ParticleEmitter::SetFrameCount(uint32_t count) { m_frameCount = count; }

GpuEmitterData ParticleEmitter::BuildGpuData(float deltaTime)
{
    m_age += deltaTime;

    // emission window: [startDelay, startDelay+duration). duration <= 0 = never stops.
    bool windowOpen = (m_age >= m_startDelay) &&
                      (m_duration <= 0.f || m_age < m_startDelay + m_duration);

    uint32_t continuous = 0u;
    if (windowOpen)
    {
        m_spawnAccumulator += m_spawnRate * deltaTime;
        continuous = static_cast<uint32_t>(m_spawnAccumulator);
        m_spawnAccumulator -= static_cast<float>(continuous);
    }

    if (m_emissionMode == EmissionMode::Burst && windowOpen)
    {
        if (!m_burstStarted) { m_burstCount += m_burstAmount; m_burstStarted = true; }  // initial puff when the window opens
        if (m_burstInterval > 0.f)
        {
            m_burstTimer += deltaTime;
            while (m_burstTimer >= m_burstInterval)
            {
                m_burstCount += m_burstAmount;
                m_burstTimer -= m_burstInterval;
            }
        }
    }

    uint32_t total = m_active ? (continuous + m_burstCount) : 0u;
    m_burstCount = 0;   // continuous + cadence + manual Burst() all consumed

    GpuEmitterData d{};
    d.position = glm::vec4(m_worldPosition, 0.f);
    d.direction = glm::vec4(m_worldDirection, 0.f);
    d.gravity = glm::vec4(m_gravity, 0.f);
    d.attractorPos = glm::vec4(m_attractorPos, m_attractorStrength);
    d.colour = m_colour;
    d.spread = m_spread;
    d.minSpeed = m_minSpeed;
    d.maxSpeed = m_maxSpeed;
    d.minLifetime = m_minLifetime;
    d.maxLifetime = m_maxLifetime;
    d.minSize = m_minSize;
    d.maxSize = m_maxSize;
    d.drag = m_drag;
    d.noiseStrength = m_noiseStrength;
    d.noiseScale = m_noiseScale;
    d.noiseScrollSpeed = m_noiseScrollSpeed;
    d.restitution = m_restitution;
    d.spawnCount = total;
    d.spawnOffset = 0;   // filled by ParticleWorld
    d.useAttractor = m_useAttractor ? 1u : 0u;
    d.blendMode = static_cast<uint32_t>(m_blendMode);
    d.useCollision = m_useCollision ? 1u : 0u;
    d.active = m_active ? 1u : 0u;
    d.textureIndex = m_textureIndex;
    d.animMode = static_cast<uint32_t>(m_animMode);
    d.frameCount = m_frameCount;
    d.velocityStretch = m_velocityStretch;
    d.animFps = m_animFps;
    d.shapeDirection = m_shapeDirection;
    d.emissionShape = glm::vec4(float(static_cast<uint32_t>(m_emissionShape)),
                                m_emissionShapeSize.x, m_emissionShapeSize.y, m_emissionShapeSize.z);
    return d;
}

void ParticleEmitter::Configure(const EmitterDesc& desc)
{
    // template / behaviour fields only — world position & direction are set
    // separately each frame (instance data from the entity transform).
    // In Burst mode the continuous rate is suppressed; the system injects
    // particles via Burst() instead.
    m_spawnRate        = (desc.emissionMode == EmissionMode::Burst) ? 0.f : desc.spawnRate;
    m_spread           = desc.spread;
    m_minSpeed         = desc.minSpeed;
    m_maxSpeed         = desc.maxSpeed;
    m_minLifetime      = desc.minLifetime;
    m_maxLifetime      = desc.maxLifetime;
    m_minSize          = desc.minSize;
    m_maxSize          = desc.maxSize;
    m_drag             = desc.drag;
    m_noiseStrength    = desc.noiseStrength;
    m_noiseScale       = desc.noiseScale;
    m_noiseScrollSpeed = desc.noiseScrollSpeed;
    m_colour           = desc.colour;
    m_blendMode        = desc.blendMode;
    m_useCollision     = desc.useCollision;
    m_restitution      = desc.restitution;
    m_useAttractor     = desc.useAttractor;
    m_attractorStrength = desc.attractorStrength;
    m_gravity          = desc.gravity;
    m_textureIndex     = desc.textureLayer;
    m_frameCount       = desc.frameCount;
    m_animMode         = desc.animMode;
    m_animFps          = desc.animFps;
    m_velocityStretch  = desc.velocityStretch;
    m_active           = desc.active;

    m_emissionMode  = desc.emissionMode;
    m_burstAmount   = desc.burstCount;
    m_burstInterval = desc.burstInterval;
    m_startDelay    = desc.startDelay;    // note: m_age is runtime, not copied here
    m_duration      = desc.duration;

    m_emissionShape     = desc.emissionShape;
    m_emissionShapeSize = desc.emissionShapeSize;
    m_shapeDirection    = desc.shapeDirection;

    m_colourStopCount = desc.colourStopCount;
    for (int i = 0; i < MAX_GRADIENT_STOPS; ++i) m_colourOverLife[i] = desc.colourOverLife[i];
    m_sizeKeyCount = desc.sizeKeyCount;
    for (int i = 0; i < MAX_CURVE_KEYS; ++i) m_sizeOverLife[i] = desc.sizeOverLife[i];
}

EmitterDesc ParticleEmitter::ToDesc() const
{
    EmitterDesc d;
    d.spawnRate        = m_spawnRate;
    d.spread           = m_spread;
    d.minSpeed         = m_minSpeed;
    d.maxSpeed         = m_maxSpeed;
    d.minLifetime      = m_minLifetime;
    d.maxLifetime      = m_maxLifetime;
    d.minSize          = m_minSize;
    d.maxSize          = m_maxSize;
    d.drag             = m_drag;
    d.noiseStrength    = m_noiseStrength;
    d.noiseScale       = m_noiseScale;
    d.noiseScrollSpeed = m_noiseScrollSpeed;
    d.colour           = m_colour;
    d.blendMode        = m_blendMode;
    d.useCollision     = m_useCollision;
    d.restitution      = m_restitution;
    d.useAttractor     = m_useAttractor;
    d.attractorStrength = m_attractorStrength;
    d.gravity          = m_gravity;
    d.textureLayer     = m_textureIndex;
    d.frameCount       = m_frameCount;
    d.animMode         = m_animMode;
    d.animFps          = m_animFps;
    d.velocityStretch  = m_velocityStretch;
    d.active           = m_active;

    d.emissionMode  = m_emissionMode;
    d.burstCount    = m_burstAmount;
    d.burstInterval = m_burstInterval;
    d.startDelay    = m_startDelay;
    d.duration      = m_duration;

    d.emissionShape     = m_emissionShape;
    d.emissionShapeSize = m_emissionShapeSize;
    d.shapeDirection    = m_shapeDirection;

    d.colourStopCount = m_colourStopCount;
    for (int i = 0; i < MAX_GRADIENT_STOPS; ++i) d.colourOverLife[i] = m_colourOverLife[i];
    d.sizeKeyCount = m_sizeKeyCount;
    for (int i = 0; i < MAX_CURVE_KEYS; ++i) d.sizeOverLife[i] = m_sizeOverLife[i];
    return d;
}