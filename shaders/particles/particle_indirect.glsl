#version 450 core
#include "common.glsl"
layout(local_size_x = 1) in;

layout(std430, binding = BIND_DEADCOUNT) coherent buffer DeadCountBuf { uint deadCount; };
layout(std430, binding = BIND_INDIRECT)  buffer IndirectBuf { DrawCommand cmd; };


void main()
{
    uint alive = u_maxParticles - deadCount;
    cmd.count = alive * 6u; //u_maxParticles * 6u;// 
    cmd.instanceCount = 1u;
    cmd.first = 0u;
    cmd.baseInstance = 0u;
}