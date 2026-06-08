#version 450 core
#include "common.glsl"
layout(local_size_x = 256) in;

layout(std430, binding = BIND_PARTICLES) buffer ParticleBuf { Particle particles[]; };
layout(std430, binding = BIND_EMITTERS)  readonly buffer EmitterBuf { EmitterData emitters[]; };


void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= u_maxParticles) return;
    if (particles[i].lifetime <= 0.0) return;

    EmitterData em = emitters[particles[i].emitterIndex];
    vec3 pos = particles[i].position.xyz;
    vec3 vel = particles[i].velocity.xyz;

    vel += em.gravity.xyz * u_deltaTime;          // world-space gravity
    vel *= exp(-em.drag * u_deltaTime);           // frame-rate-independent drag

    if (em.noiseStrength > 0.0)
    {
        vec3 np = pos * em.noiseScale + vec3(u_time * em.noiseScrollSpeed);
        vel += curlNoise(np) * em.noiseStrength * u_deltaTime;   // divergence-free turbulence
    }

    if (em.useAttractor == 1u)
    {
        vec3 to = em.attractorPos.xyz - pos;
        float d = length(to);
        if (d > 1e-4) vel += (to / d) * em.attractorPos.w * u_deltaTime;
    }

    pos += vel * u_deltaTime;
    particles[i].position = vec4(pos, 0.0);
    particles[i].velocity = vec4(vel, 0.0);
}