#version 450 core
#include "common.glsl"
layout(local_size_x = 256) in;

layout(std430, binding = BIND_HISTOGRAM) buffer HistBuf { uint histogram[]; };


shared uint s_scan[256];

void main()
{
    uint b = gl_LocalInvocationID.x;       // bucket
    uint groups = u_maxParticles / 256u;

    uint total = 0u;                        // this bucket's total across all groups
    for (uint g = 0u; g < groups; ++g) total += histogram[b * groups + g];

    // exclusive scan of bucket totals -> bucket base offsets
    s_scan[b] = total;
    barrier();
    for (uint off = 1u; off < 256u; off <<= 1)
    {
        uint v = (b >= off) ? s_scan[b - off] : 0u;
        barrier();
        s_scan[b] += v;
        barrier();
    }
    uint bucketBase = s_scan[b] - total;    // inclusive -> exclusive

    // write exclusive per-(bucket,group) start offsets back into the histogram
    uint running = bucketBase;
    for (uint g = 0u; g < groups; ++g)
    {
        uint c = histogram[b * groups + g];
        histogram[b * groups + g] = running;
        running += c;
    }
}