#version 450 core
#include "common.glsl"
layout(local_size_x = 256) in;

layout(std430, binding = BIND_PARTICLES) buffer ParticleBuf { Particle particles[]; };
layout(std430, binding = BIND_DEADLIST)  buffer DeadListBuf { uint deadList[]; };
layout(std430, binding = BIND_DEADCOUNT) coherent buffer DeadCountBuf { uint deadCount; };
layout(std430, binding = BIND_EMITTERS)  readonly buffer EmitterBuf { EmitterData emitters[]; };

uniform uint  u_spawnCount;

void main()
{
    uint t = gl_GlobalInvocationID.x;
    if (t >= u_spawnCount) return;

    // which emitter owns spawn t? scan the spawnOffset ranges (<=64 emitters)
    uint e = 0u; bool found = false;
    for (uint k = 0u; k < u_emitterCount; ++k)
    {
        uint off = emitters[k].spawnOffset;
        uint cnt = emitters[k].spawnCount;
        if (t >= off && t < off + cnt) { e = k; found = true; break; }
    }
    if (!found) return;

    EmitterData em = emitters[e];

    // pop a free slot (LIFO). old == value before decrement.
    uint old = atomicAdd(deadCount, uint(-1));
    if (old == 0u || old > u_maxParticles) { atomicAdd(deadCount, 1u); return; } // empty / underflow
    uint idx = deadList[old - 1u];

    uint seed = (idx * 2654435761u) ^ (t * 40503u) ^ floatBitsToUint(u_time);

    float speed = randRange(seed + 1u, em.minSpeed,    em.maxSpeed);
    float life  = randRange(seed + 2u, em.minLifetime, em.maxLifetime);
    float size  = randRange(seed + 3u, em.minSize,     em.maxSize);
    vec3  dir   = randomConeDir(normalize(em.direction.xyz), em.spread, seed + 4u);

    // distribute the spawn position within the emission shape
    vec3  offset = sampleShapeOffset(uint(em.emissionShape.x), em.emissionShape.yzw,
                                     em.direction.xyz, seed + 5u);

    // optionally bias the launch direction outward from the shape (radial)
    if (em.shapeDirection > 0.0 && dot(offset, offset) > 1e-8)
        dir = normalize(mix(dir, normalize(offset), em.shapeDirection));

 
    float packedTint = 0.0;
    vec3  variance   = em.colourVariance.rgb;
    if (any(greaterThan(variance, vec3(0.0))))
    {
        vec3 tint = vec3(randRange(seed + 30u, 1.0 - variance.r, 1.0 + variance.r),
                         randRange(seed + 31u, 1.0 - variance.g, 1.0 + variance.g),
                         randRange(seed + 32u, 1.0 - variance.b, 1.0 + variance.b));
        packedTint = uintBitsToFloat(packUnorm4x8(vec4(clamp(tint, 0.0, 2.0) * 0.5, 1.0)));
    }

    particles[idx].position     = vec4(em.position.xyz + offset, packedTint);
    particles[idx].velocity     = vec4(dir * speed, 0.0);
    particles[idx].lifetime     = life;
    particles[idx].maxLifetime  = life;
    particles[idx].size         = size;
    particles[idx].emitterIndex = e;
}