#version 450 core
#include "common.glsl"
layout(local_size_x = 1) in;

layout(std430, binding = BIND_SORTCOUNT)    readonly  buffer SortCountBuf { uint sortCount; };
layout(std430, binding = BIND_INDIRECT)     writeonly buffer IndirectBuf  { DrawCommand cmd; };
layout(std430, binding = BIND_SORTDISPATCH) writeonly buffer DispatchBuf  { uint groupsX; uint groupsY; uint groupsZ; };


void main()
{
    uint alive = sortCount;

    cmd.count = alive * 6u;
    cmd.instanceCount = 1u;
    cmd.first = 0u;
    cmd.baseInstance = 0u;

    groupsX = (alive + 255u) / 256u;
    groupsY = 1u;
    groupsZ = 1u;
}
