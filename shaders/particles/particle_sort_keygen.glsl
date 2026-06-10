#version 450 core
#include "common.glsl"
layout(local_size_x = 256) in;

layout(std430, binding = BIND_PARTICLES) readonly buffer ParticleBuf { Particle particles[]; };
layout(std430, binding = BIND_SORTKEYS)  writeonly buffer SortBuf { SortEntry sortKeys[]; };
layout(std430, binding = BIND_SORTCOUNT) coherent buffer SortCountBuf { uint sortCount; };


void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= u_maxParticles) return;
    if (particles[i].lifetime <= 0.0) return;

    float vz = (u_view * vec4(particles[i].position.xyz, 1.0)).z; // more negative = farther

    uint slot = atomicAdd(sortCount, 1u);
    sortKeys[slot].particleIndex = i;
    sortKeys[slot].depthBits = floatToSortable(vz);               // ascending = back-to-front
}
