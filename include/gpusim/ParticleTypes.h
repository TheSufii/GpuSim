#pragma once 

#include <glm/glm.hpp>
#include <cstdint>

struct Particle {
	glm::vec4 position;           // xyz = world pos,      w = packed per-particle tint (packUnorm4x8; 0 = neutral)
	glm::vec4 velocity;           // xyz = world vel,      w = unused
	float lifetime;
	float maxLifetime;
	float size;
	uint32_t emitterIndex;        // indexes into GpuEmitterData array
};

enum class BlendMode : uint32_t {
	Additive = 0, 
	AlphaBlend = 1
};

enum class AnimMode : uint32_t
{
    PlayOnce = 0,                 // frame = lifetimeRatio * totalFrames � bolt flashes once and dies
    Loop = 1                      // frame = mod(age * fps, totalFrames) � rolling smoke
};


enum class EmissionMode : uint32_t
{
    Continuous = 0,               // steady stream driven by spawnRate
    Burst = 1                     // spawnRate ignored; emits burstCount in lumps
};

// Where particles are born (offset from the emitter position). Oriented by the
// emit direction (direction = the shape's local +Y) and the tangent (its local +X).
enum class EmissionShape : uint32_t
{
    Point  = 0,                   // single point (legacy behaviour)
    Sphere = 1,                   // solid sphere, radius = size.x
    Box    = 2,                   // box, half-extents = size.xyz
    Disc   = 3                    // disc perpendicular to direction, radius = size.x
};

// sentinel: "no texture, use the procedural circle fallback"
constexpr uint32_t kNoTexture = 0xFFFFFFFFu;

struct GpuEmitterData
{
    // vec4 rows (each 16-byte aligned)
    glm::vec4 position;           // xyz = world pos
    glm::vec4 direction;          // xyz = world emit direction
    glm::vec4 gravity;            // xyz = world gravity vector, magnitude = strength
    glm::vec4 attractorPos;       // xyz = world pos, w = strength (+pull / -push)
    glm::vec4 colour;             // rgba tint

    // scalar row 1
    float     spread;
    float     minSpeed;
    float     maxSpeed;
    float     minLifetime;

    // scalar row 2
    float     maxLifetime;
    float     minSize;
    float     maxSize;
    float     drag;               // velocity damping 0 = none

    // scalar row 3
    float     noiseStrength;
    float     noiseScale;
    float     noiseScrollSpeed;
    float     restitution;

    // uint row 1
    uint32_t  spawnCount;         // particles this emitter is to spawn this frame
    uint32_t  spawnOffset;
    uint32_t  useAttractor;
    uint32_t  blendMode;

    // uint row 2
    uint32_t  useCollision;
    uint32_t  active;
    uint32_t  textureIndex;       // texture array layer; kNoTexture = procedural circle
    uint32_t  animMode;           // AnimMode cast to uint

    uint32_t  frameCount;         // contiguous full-res layers from textureIndex; 1 = static
    float     velocityStretch;    // 0 = camera-facing; >0 = stretch billboard along velocity
    float     animFps;            // frames/sec for AnimMode::Loop (ignored by PlayOnce)
    float     shapeDirection;     // 0 = launch along direction, 1 = radial out from shape

    // emission shape: x = EmissionShape, yzw = size (radius / half-extents)
    glm::vec4 emissionShape;

    glm::vec4 colourVariance;     // rgb = per-channel random ± amount at spawn (0 = none); w reserved
    glm::vec4 collisionParams;    // x = thickness gate, y = surface separation; zw reserved
    glm::vec4 tangent;            // xyz = the emission shape's local +X in world space; zero = derive from direction
};

struct DrawArraysIndirectCommand
{
    uint32_t count;          // aliveCount * 6, written by GPU each frame
    uint32_t instanceCount;  // always 1
    uint32_t first;          // always 0
    uint32_t baseInstance;   // always 0
};

struct SortEntry
{
    uint32_t particleIndex;
    uint32_t depthBits;      // float depth transformed to sortable uint
};

struct WorldParams
{
    float    deltaTime;
    float    time;
    uint32_t maxParticles;
    uint32_t emitterCount;
};

struct CameraParams
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 invView;     // pre-inverted to avoid per-thread inverse() in shaders
    glm::mat4 invProj;
};



// if these fire, the struct layout no longer matches std430
static_assert(sizeof(Particle) == 48, "Particle layout mismatch");
static_assert(sizeof(GpuEmitterData) == 240, "GpuEmitterData layout mismatch");
static_assert(sizeof(DrawArraysIndirectCommand) == 16, "DrawCommand layout mismatch");
static_assert(sizeof(SortEntry) == 8, "SortEntry layout mismatch");
static_assert(sizeof(WorldParams) == 16, "WorldParams layout mismatch");
static_assert(sizeof(CameraParams) == 256, "CameraParams layout mismatch");
