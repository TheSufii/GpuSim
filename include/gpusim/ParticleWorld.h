#pragma once
#include "GpuSimulation.h"
#include "ParticleEmitter.h"
#include "ParticleTypes.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>

class ParticleWorldRegistry;

class ParticleWorld : public GpuSimulation
{
public:
    ParticleWorld() = default;
    ~ParticleWorld() override;

    // maxParticles must be a multiple of 256 (thread group size).
    // shaderDir is cached and used for internal (keygen/sort/collision) shaders.
    void Initialize(uint32_t maxParticles, const char* shaderDir);

    // Loads core shaders, adds spawn/update/lifetime/indirect
    // stages and the default billboard render stage. User provides billboard shader.
    void SetupDefault(GLuint billboardShader);

    void SetCollisionEnabled(bool b) { m_collisionEnabled = b; }

    void SetDepthTexture(GLuint tex) { m_depthTexture = tex; }

    const GLuint* DepthTextureRef() const { return &m_depthTexture; }

    // soft particle / linearization params
    void SetNearFar(float n, float f) { m_near = n; m_far = f; }
    void SetSoftFadeDistance(float d) { m_softFade = d; }

    // camera — single source of truth for collision, keygen, and billboard shaders
    void SetCamera(const glm::mat4& view, const glm::mat4& proj);

    GLuint CreateTextureArray(const std::vector<const uint8_t*>& layerPixels, uint32_t resolution);

    void SetParticleTextureArray(GLuint arr) { m_textureArray = arr; m_ownsTextureArray = false; }

    // nullptr when every slot is taken (destroyed slots stay taken until their particles die)
    ParticleEmitter* CreateEmitter();
    ParticleEmitter* CreateEmitter(const EmitterDesc& desc);   // create, then Configure
    void DestroyEmitter(ParticleEmitter* e);

    // Removes every emitter and kills every live particle, so every slot is free at once.
    // For a scene reload, where DestroyEmitter would hold each old slot until its particles die.
    // Invalidates every emitter pointer and index handed out so far.
    void Clear();

    // indices are stable (DestroyEmitter deactivates but keeps the slot)
    ParticleEmitter*       GetEmitter(uint32_t index)       { return index < m_emitters.size() ? m_emitters[index].get() : nullptr; }
    const ParticleEmitter* GetEmitter(uint32_t index) const { return index < m_emitters.size() ? m_emitters[index].get() : nullptr; }

    void Dispatch(float deltaTime) override;
    void Render()                  override;

    uint32_t GetEmitterCount() const { return (uint32_t)m_emitters.size(); }

    // --- editor / debug introspection (read-only) ---

    // Adds this world to a registry and names it. The world auto-unregisters
    // when destroyed, so the registry never dangles.
    void RegisterWith(ParticleWorldRegistry& registry, const std::string& name);
    const std::string& GetName() const { return m_name; }

    uint32_t GetActiveEmitterCount() const;        // emitters with active == true
    uint32_t GetLastSpawnCount()     const { return m_lastSpawnCount; }  // spawned last Dispatch

    // alive = capacity - deadCount. Non-blocking: returns the most recent
    // completed async copy (a frame or two old) and kicks off the next one,
    // so it never stalls the pipeline. Safe to call every frame.
    uint32_t ReadbackAliveCount() const;

private:
    void InitializeDeadList();
    void InitializeSortBuffers();
    void InitializeEmitterBuffer();
    void InitializeUBOs();
    void LoadInternalShaders();         

    void Finalize();                     // appends internal stages once, before first dispatch

    uint32_t UploadEmitterData(float dt); // returns total spawn count this frame
    void     UploadWorldParams(float dt);
    void     BindUBOs();

    // buffers
    GLuint m_deadListBuffer = 0;
    GLuint m_deadCountBuffer = 0;
    GLuint m_sortBuffer = 0;
    GLuint m_sortBufferAlt = 0;
    GLuint m_histogramBuffer = 0;
    GLuint m_sortCountBuffer = 0;     // alive entries compacted by keygen (zeroed each frame)
    GLuint m_sortDispatchBuffer = 0;  // dispatch-indirect args for the radix passes
    GLuint m_emitterBuffer = 0;
    GLuint m_worldUBO = 0;
    GLuint m_cameraUBO = 0;

    // async alive-count readback
    mutable GLuint          m_aliveReadback = 0;
    mutable const uint32_t* m_aliveMapped = nullptr;
    mutable GLsync          m_aliveFence = 0;
    mutable uint32_t        m_cachedDeadCount = 0xFFFFFFFFu;   // > max until first result -> alive 0

    // internal shaders
    GLuint m_keygenShader = 0;
    GLuint m_radixCountShader = 0;
    GLuint m_radixPrefixShader = 0;
    GLuint m_radixScatterShader = 0;
    GLuint m_indirectShader = 0;
    GLuint m_collisionShader = 0;

    // collision / soft state
    bool   m_collisionEnabled = false;  
    GLuint m_depthTexture = 0;
    float  m_near = 0.1f;
    float  m_far = 1000.f;
    float  m_softFade = 0.5f;

    // textures
    GLuint m_textureArray = 0;
    bool   m_ownsTextureArray = false;

    // lifetime-curve LUT atlases (one row per emitter), baked each frame
    void   InitializeLifetimeLUTs();
    void   BakeLifetimeLUTs();
    GLuint m_lutColour = 0;   // RGBA8  k_lutWidth x k_maxEmitters : colour+alpha over life
    GLuint m_lutScalar = 0;   // R32F   k_lutWidth x k_maxEmitters : size over life
    static constexpr uint32_t k_lutWidth = 64;

    bool   m_finalized = false;
    float  m_totalTime = 0.f;
    std::string m_shaderDir;

    // editor / debug introspection
    ParticleWorldRegistry* m_registry = nullptr;   // non-owning; for auto-unregister
    std::string            m_name = "Particle World";
    uint32_t               m_lastSpawnCount = 0;

    static constexpr uint32_t k_maxEmitters = 128;
    static constexpr uint32_t k_radixBuckets = 256;
    static constexpr uint32_t k_threadGroup = 256;

    std::vector<std::unique_ptr<ParticleEmitter>> m_emitters;

    // recycled emitter slots: index + the time after which its particles are gone
    struct FreeSlot { uint32_t index; float reuseAfter; };
    std::vector<FreeSlot> m_freeSlots;
};