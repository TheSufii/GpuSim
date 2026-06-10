#version 450 core
#include "common.glsl"
layout(local_size_x = 256) in;

layout(std430, binding = BIND_SORTKEYS)     buffer SortBuf    { SortEntry sortKeys[]; };
layout(std430, binding = BIND_SORTKEYS_ALT) buffer SortAltBuf { SortEntry sortKeysAlt[]; };
layout(std430, binding = BIND_HISTOGRAM)    readonly buffer HistBuf { uint histogram[]; };
layout(std430, binding = BIND_SORTCOUNT)    readonly buffer SortCountBuf { uint sortCount; };

uniform uint u_bitShift;
uniform uint u_pingPong;

shared uint s_digit[256];


void main()
{
    uint local  = gl_LocalInvocationID.x;
    uint global = gl_GlobalInvocationID.x;
    uint group  = gl_WorkGroupID.x;
    uint groups = (sortCount + 255u) / 256u;

    bool valid = global < sortCount;

    SortEntry e;
    uint digit = 0xFFFFFFFFu;
    if (valid)
    {
        e = (u_pingPong == 0u) ? sortKeys[global] : sortKeysAlt[global];
        digit = (e.depthBits >> u_bitShift) & 0xFFu;
    }
    s_digit[local] = digit;
    barrier();

    if (!valid) return;

    // stable: rank among earlier threads in this group sharing the digit
    uint rank = 0u;
    for (uint t = 0u; t < local; ++t) if (s_digit[t] == digit) ++rank;

    uint dst = histogram[digit * groups + group] + rank;

    if (u_pingPong == 0u) sortKeysAlt[dst] = e;
    else                  sortKeys[dst]    = e;
}
