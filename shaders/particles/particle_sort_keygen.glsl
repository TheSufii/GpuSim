#version 450 core
#include "common.glsl"
layout(local_size_x = 256) in;

layout(std430, binding = BIND_PARTICLES) readonly buffer ParticleBuf { Particle particles[]; };
layout(std430, binding = BIND_SORTKEYS)  buffer SortBuf { SortEntry sortKeys[]; };

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= u_maxParticles) return;

    sortKeys[i].particleIndex = i;

    if (particles[i].lifetime <= 0.0) { sortKeys[i].depthBits = 0xFFFFFFFFu; return; }

    float vz = (u_view * vec4(particles[i].position.xyz, 1.0)).z; // more negative = farther
    sortKeys[i].depthBits = floatToSortable(vz);                  // ascending = back-to-front
}