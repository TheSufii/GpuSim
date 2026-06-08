#pragma once
#include "ParticleTypes.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <type_traits>

constexpr int MAX_GRADIENT_STOPS = 8;
constexpr int MAX_CURVE_KEYS     = 8;

// Per-segment interpolation shape. A stop/key's ease shapes the segment ENTERING
// it (stop[i].ease controls the curve from stop[i-1] to stop[i]).
enum class EaseMode : uint32_t { Linear = 0, Smooth = 1, EaseIn = 2, EaseOut = 3, Hold = 4 };

struct GradientStop { float t = 0.f; glm::vec4 colour = glm::vec4(1.f); EaseMode ease = EaseMode::Linear; };
struct CurveKey     { float t = 0.f; float     value  = 1.f;            EaseMode ease = EaseMode::Linear; };

// remap normalised segment position u in [0,1] by the chosen ease
inline float ApplyEase(EaseMode m, float u)
{
    switch (m)
    {
    case EaseMode::Smooth:  return u * u * (3.f - 2.f * u);   // smoothstep (ease in-out)
    case EaseMode::EaseIn:  return u * u;
    case EaseMode::EaseOut: return u * (2.f - u);
    case EaseMode::Hold:    return 0.f;                       // step: hold the left value
    default:                return u;                         // Linear
    }
}


struct EmitterDesc
{
    EmitterDesc();   // fills the default lifetime curves

    // emission
    EmissionMode emissionMode  = EmissionMode::Continuous;
    float        spawnRate     = 100.f;   // Continuous: particles/sec
    uint32_t     burstCount    = 32;      // Burst: particles per burst
    float        burstInterval = 0.f;     // Burst: seconds between bursts (0 = one-shot on spawn)
    float        startDelay    = 0.f;     // seconds before this emitter starts emitting
    float        duration      = 0.f;     // seconds it emits, then stops (<= 0 = forever)

    EmissionShape emissionShape     = EmissionShape::Point;
    glm::vec3     emissionShapeSize = glm::vec3(0.5f);  // radius (Sphere/Disc) or half-extents (Box)
    float         shapeDirection    = 0.f;              // 0 = along direction, 1 = radial from shape

    float spread       = 0.3f;
    float minSpeed     = 1.f,   maxSpeed    = 3.f;
    float minLifetime  = 1.f,   maxLifetime = 2.f;
    float minSize      = 0.05f, maxSize     = 0.1f;

    float drag             = 0.f;
    float noiseStrength    = 0.f;
    float noiseScale       = 1.f;
    float noiseScrollSpeed = 0.f;

    glm::vec4 colour    = glm::vec4(1.f);   // overall multiplier on the colour-over-life gradient
    BlendMode blendMode = BlendMode::AlphaBlend;

    bool  useCollision  = false;
    float restitution   = 0.3f;

    bool      useAttractor      = false;
    float     attractorStrength = 0.f;
    glm::vec3 attractorOffset   = glm::vec3(0.f);   // local; resolved to world by the caller

    glm::vec3 gravity   = glm::vec3(0.f, -9.8f, 0.f);

    // visuals
    uint32_t textureLayer  = kNoTexture;   // base layer; kNoTexture = procedural circle
    uint32_t frameCount    = 1;            // contiguous layers from textureLayer (1 = static)
    AnimMode animMode      = AnimMode::PlayOnce;
    float    animFps       = 12.f;
    float    velocityStretch = 0.f;        // 0 = camera-facing; >0 = stretch along velocity

    bool active = true;

    GradientStop colourOverLife[MAX_GRADIENT_STOPS];   // rgba gradient over life
    uint32_t     colourStopCount = 4;
    CurveKey     sizeOverLife[MAX_CURVE_KEYS];          // size multiplier over life
    uint32_t     sizeKeyCount = 2;
};

inline EmitterDesc::EmitterDesc()
{
    // default colour gradient = fade in, hold white, fade out (matches the old
    // hardcoded shader fade, so switching to the LUT changes nothing by default).
    colourOverLife[0] = { 0.00f, glm::vec4(1.f, 1.f, 1.f, 0.f) };
    colourOverLife[1] = { 0.10f, glm::vec4(1.f, 1.f, 1.f, 1.f) };
    colourOverLife[2] = { 0.85f, glm::vec4(1.f, 1.f, 1.f, 1.f) };
    colourOverLife[3] = { 1.00f, glm::vec4(1.f, 1.f, 1.f, 0.f) };
    colourStopCount = 4;

    // default size curve = constant 1x
    sizeOverLife[0] = { 0.0f, 1.0f };
    sizeOverLife[1] = { 1.0f, 1.0f };
    sizeKeyCount = 2;
}

// Evaluate a curve at t in [0,1] by linear interpolation between bracketing
// stops/keys (assumes sorted by t). Shared by the LUT bake and the editor preview.
inline glm::vec4 EvalGradient(const GradientStop* stops, uint32_t count, float t)
{
    if (count == 0) return glm::vec4(1.f);
    if (t <= stops[0].t) return stops[0].colour;
    for (uint32_t i = 1; i < count; ++i)
        if (t <= stops[i].t)
        {
            float span = stops[i].t - stops[i - 1].t;
            float u = span > 1e-6f ? (t - stops[i - 1].t) / span : 0.f;
            return glm::mix(stops[i - 1].colour, stops[i].colour, ApplyEase(stops[i].ease, u));
        }
    return stops[count - 1].colour;
}

inline float EvalCurve(const CurveKey* keys, uint32_t count, float t)
{
    if (count == 0) return 1.f;
    if (t <= keys[0].t) return keys[0].value;
    for (uint32_t i = 1; i < count; ++i)
        if (t <= keys[i].t)
        {
            float span = keys[i].t - keys[i - 1].t;
            float u = span > 1e-6f ? (t - keys[i - 1].t) / span : 0.f;
            return glm::mix(keys[i - 1].value, keys[i].value, ApplyEase(keys[i].ease, u));
        }
    return keys[count - 1].value;
}

// the cooked .demitter writes EmitterDesc raw, so it must stay trivially copyable
static_assert(std::is_trivially_copyable_v<EmitterDesc>,
              "EmitterDesc must stay trivially copyable for raw .demitter serialization");
