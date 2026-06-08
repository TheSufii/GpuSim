#version 450 core
#include "common.glsl"
layout(local_size_x = 256) in;

layout(std430, binding = BIND_PARTICLES) buffer ParticleBuf { Particle particles[]; };
layout(std430, binding = BIND_DEADLIST)  buffer DeadListBuf { uint deadList[]; };
layout(std430, binding = BIND_DEADCOUNT) coherent buffer DeadCountBuf { uint deadCount; };


void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= u_maxParticles) return;
    if (particles[i].lifetime <= 0.0) return;

    float life = particles[i].lifetime - u_deltaTime;
    particles[i].lifetime = max(life, 0.0);

    if (life <= 0.0)
    {
        uint slot = atomicAdd(deadCount, 1u);   // push slot back (returns old top)
        deadList[slot] = i;
    }
}