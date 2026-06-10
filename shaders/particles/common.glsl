// ---- binding points (mirror SimBindings.h) ----
#define BIND_PARTICLES      0
#define BIND_DEADLIST       1
#define BIND_DEADCOUNT      2
#define BIND_INDIRECT       3
#define BIND_SORTKEYS       4
#define BIND_EMITTERS       5
#define BIND_SORTKEYS_ALT   6
#define BIND_HISTOGRAM      7
#define BIND_SORTCOUNT      8
#define BIND_SORTDISPATCH   9

#define BIND_CAMERA_UBO     0
#define BIND_WORLD_UBO      1

// ---- constants / sentinels (mirror ParticleTypes.h) ----
#define NO_TEXTURE     0xFFFFFFFFu
#define BLEND_ADDITIVE 0u
#define BLEND_ALPHA    1u
#define ANIM_PLAYONCE  0u
#define ANIM_LOOP      1u

// lifetime LUT atlas height (mirror k_maxEmitters in ParticleWorld.h)
#define MAX_EMITTERS   64u

// emission shapes (mirror EmissionShape in ParticleTypes.h)
#define SHAPE_POINT    0u
#define SHAPE_SPHERE   1u
#define SHAPE_BOX      2u
#define SHAPE_DISC     3u

// ---- structs (must match std430 layout in ParticleTypes.h) ----
struct Particle
{
    vec4  position;     // xyz = world pos, w = packed per-particle tint (packUnorm4x8; 0 = neutral)
    vec4  velocity;     // xyz = world vel
    float lifetime;
    float maxLifetime;
    float size;
    uint  emitterIndex;
};

struct EmitterData
{
    vec4 position;
    vec4 direction;
    vec4 gravity;
    vec4 attractorPos;   // xyz = pos, w = strength
    vec4 colour;

    float spread;
    float minSpeed;
    float maxSpeed;
    float minLifetime;

    float maxLifetime;
    float minSize;
    float maxSize;
    float drag;

    float noiseStrength;
    float noiseScale;
    float noiseScrollSpeed;
    float restitution;

    uint spawnCount;
    uint spawnOffset;
    uint useAttractor;
    uint blendMode;

    uint useCollision;
    uint isActive;
    uint textureIndex;
    uint animMode;

    uint  frameCount;        // contiguous full-res layers from textureIndex; 1 = static
    float velocityStretch;   // 0 = camera-facing; >0 = stretch along velocity by speed
    float animFps;
    float shapeDirection;    // 0 = launch along direction, 1 = radial out from shape

    vec4  emissionShape;     // x = EmissionShape, yzw = size (radius / half-extents)

    vec4  colourVariance;    // rgb = per-channel random ± amount at spawn (0 = none); w reserved
    vec4  collisionParams;   // x = thickness gate, y = surface separation; zw reserved
};

struct SortEntry { uint particleIndex; uint depthBits; };
struct DrawCommand { uint count; uint instanceCount; uint first; uint baseInstance; };

// ---- camera (std140 UBO) ----
layout(std140, binding = BIND_CAMERA_UBO) uniform CameraUBO
{
    mat4 u_view;
    mat4 u_proj;
    mat4 u_invView;
    mat4 u_invProj;
};

layout(std140, binding = BIND_WORLD_UBO) uniform WorldUBO
{
    float u_deltaTime;
    float u_time;
    uint  u_maxParticles;
    uint  u_emitterCount;
};


uint hashU(uint x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16; return x;
}
float rand01(uint s)              { return float(hashU(s) & 0x00FFFFFFu) / float(0x01000000u); }
float randRange(uint s, float a, float b) { return a + rand01(s) * (b - a); }

// map a float so raw-uint ascending order == float ascending order
uint floatToSortable(float f)
{
    uint b = floatBitsToUint(f);
    uint mask = uint(-int(b >> 31)) | 0x80000000u;
    return b ^ mask;
}

// random unit vector inside a cone around dir (half-angle = spread)
vec3 randomConeDir(vec3 dir, float spread, uint seed)
{
    float cosA = cos(spread);
    float z    = randRange(seed,        cosA, 1.0);
    float phi  = randRange(seed + 101u, 0.0, 6.2831853);
    float s    = sqrt(max(0.0, 1.0 - z * z));
    vec3 up = abs(dir.y) < 0.999 ? vec3(0,1,0) : vec3(1,0,0);
    vec3 tan = normalize(cross(up, dir));
    vec3 bit = cross(dir, tan);
    return normalize(tan * (s * cos(phi)) + bit * (s * sin(phi)) + dir * z);
}

// random spawn offset within an emission shape, oriented by dir (= local +Y)
vec3 sampleShapeOffset(uint shape, vec3 size, vec3 dir, uint seed)
{
    if (shape == SHAPE_POINT) return vec3(0.0);

    vec3 d  = normalize(dir);
    vec3 up = abs(d.y) < 0.999 ? vec3(0,1,0) : vec3(1,0,0);
    vec3 t  = normalize(cross(up, d));
    vec3 b  = cross(d, t);

    if (shape == SHAPE_BOX)
        return t * randRange(seed + 10u, -size.x, size.x)
             + d * randRange(seed + 11u, -size.y, size.y)
             + b * randRange(seed + 12u, -size.z, size.z);

    if (shape == SHAPE_DISC)
    {
        float a = randRange(seed + 13u, 0.0, 6.2831853);
        float r = size.x * sqrt(rand01(seed + 14u));        // uniform over area
        return (t * cos(a) + b * sin(a)) * r;
    }

    // SHAPE_SPHERE: uniform within a solid sphere
    float rr = size.x * pow(rand01(seed + 15u), 0.33333333);
    float zz = randRange(seed + 16u, -1.0, 1.0);
    float aa = randRange(seed + 17u, 0.0, 6.2831853);
    float ss = sqrt(max(0.0, 1.0 - zz * zz));
    return vec3(ss * cos(aa), ss * sin(aa), zz) * rr;
}

// 3D value noise in [-1,1]
uint  h3(ivec3 p) { return hashU(uint(p.x)*0x8da6b343u ^ uint(p.y)*0xd8163841u ^ uint(p.z)*0xcb1ab31fu); }
float corner(ivec3 c) { return float(h3(c) & 0x00FFFFFFu) / float(0x01000000u); }
float vnoise(vec3 p)
{
    ivec3 i = ivec3(floor(p));
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000=corner(i+ivec3(0,0,0)), n100=corner(i+ivec3(1,0,0));
    float n010=corner(i+ivec3(0,1,0)), n110=corner(i+ivec3(1,1,0));
    float n001=corner(i+ivec3(0,0,1)), n101=corner(i+ivec3(1,0,1));
    float n011=corner(i+ivec3(0,1,1)), n111=corner(i+ivec3(1,1,1));
    float x00=mix(n000,n100,f.x), x10=mix(n010,n110,f.x);
    float x01=mix(n001,n101,f.x), x11=mix(n011,n111,f.x);
    return mix(mix(x00,x10,f.y), mix(x01,x11,f.y), f.z) * 2.0 - 1.0;
}


vec3 curlNoise(vec3 p)
{
    const float e = 0.1;                       // finite-difference step (noise space)
    const vec3  oY = vec3(31.4, 0.0, 0.0);     // offset -> potential.y field
    const vec3  oZ = vec3(0.0, 17.2, 0.0);     // offset -> potential.z field
    vec3 dx = vec3(e,0,0), dy = vec3(0,e,0), dz = vec3(0,0,e);

    
    float x = (vnoise(p+dy+oZ) - vnoise(p-dy+oZ)) - (vnoise(p+dz+oY) - vnoise(p-dz+oY));
    float y = (vnoise(p+dz)    - vnoise(p-dz))    - (vnoise(p+dx+oZ) - vnoise(p-dx+oZ));
    float z = (vnoise(p+dx+oY) - vnoise(p-dx+oY)) - (vnoise(p+dy)    - vnoise(p-dy));
    return vec3(x, y, z) / (2.0 * e);
}