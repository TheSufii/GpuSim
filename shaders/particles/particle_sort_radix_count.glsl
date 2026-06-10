#version 450 core
#include "common.glsl"
layout(local_size_x = 256) in;

layout(std430, binding = BIND_SORTKEYS)     readonly buffer SortBuf    { SortEntry sortKeys[]; };
layout(std430, binding = BIND_SORTKEYS_ALT) readonly buffer SortAltBuf { SortEntry sortKeysAlt[]; };
layout(std430, binding = BIND_HISTOGRAM)    buffer HistBuf { uint histogram[]; };
layout(std430, binding = BIND_SORTCOUNT)    readonly buffer SortCountBuf { uint sortCount; };

uniform uint u_bitShift;
uniform uint u_pingPong;   // which buffer currently holds the data

shared uint s_hist[256];


void main()
{
    uint local  = gl_LocalInvocationID.x;
    uint global = gl_GlobalInvocationID.x;
    uint group  = gl_WorkGroupID.x;
    uint groups = (sortCount + 255u) / 256u;

    s_hist[local] = 0u;
    barrier();

    if (global < sortCount)
    {
        uint key = (u_pingPong == 0u) ? sortKeys[global].depthBits : sortKeysAlt[global].depthBits;
        uint digit = (key >> u_bitShift) & 0xFFu;
        atomicAdd(s_hist[digit], 1u);
    }
    barrier();

    histogram[local * groups + group] = s_hist[local];   // local == digit here
}
