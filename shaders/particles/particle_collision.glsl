#version 450 core
#include "common.glsl"

layout(local_size_x = 256) in;
layout(std430, binding = BIND_PARTICLES) buffer ParticleBuf { Particle particles[]; };
layout(std430, binding = BIND_EMITTERS)  readonly buffer EmitterBuf { EmitterData emitters[]; };
layout(std430, binding = BIND_DEADLIST)  buffer DeadListBuf { uint deadList[]; };
layout(std430, binding = BIND_DEADCOUNT) coherent buffer DeadCountBuf { uint deadCount; };

layout(binding = 0) uniform sampler2D u_depthBuffer;   // DepthTextureUnit

const float k_collisionThickness = 0.75; 

const float k_separation         = 0.02; 


vec3 worldFromUV(vec2 uv, float d)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 v = u_invProj * clip; v /= v.w;
    return (u_invView * v).xyz;
}

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= u_maxParticles)          return;
    if (particles[i].lifetime <= 0.0) return;

    EmitterData em = emitters[particles[i].emitterIndex];
    if (em.useCollision == 0u)        return;

    vec3 pos  = particles[i].position.xyz;
    vec4 clip = u_proj * u_view * vec4(pos, 1.0);
    if (clip.w <= 0.0)                return;   // behind camera

    vec3 ndc = clip.xyz / clip.w;
    if (any(greaterThan(abs(ndc), vec3(1.0)))) return; // off-screen

    vec2  uv         = ndc.xy * 0.5 + 0.5;
    float sceneDepth = texture(u_depthBuffer, uv).r;
    float partDepth  = ndc.z * 0.5 + 0.5;

    if (partDepth <= sceneDepth)      return;   // in front of geometry, no contact

    // world point on the visible surface along this view ray
    vec3 sWorld = worldFromUV(uv, sceneDepth);

    // thickness gate � skip if merely occluded by something closer, not actually touching
    if (distance(pos, sWorld) > k_collisionThickness) return;

    // real geometric normal from two reconstructed surface tangents
    vec2 texel = 1.0 / vec2(textureSize(u_depthBuffer, 0));
    vec3 pR = worldFromUV(uv + vec2(texel.x, 0.0), texture(u_depthBuffer, uv + vec2(texel.x, 0.0)).r);
    vec3 pU = worldFromUV(uv + vec2(0.0, texel.y), texture(u_depthBuffer, uv + vec2(0.0, texel.y)).r);
    vec3 n = normalize(cross(pR - sWorld, pU - sWorld));

    vec3 camPos = u_invView[3].xyz;
    if (dot(n, camPos - sWorld) < 0.0) n = -n;

    vec3 vel = particles[i].velocity.xyz;

    vec3 reflected = reflect(vel, n) * em.restitution;
    if (length(reflected) < 0.5)            // settled - kill it AND recycle its slot
    {
        particles[i].lifetime = 0.0;
        uint slot = atomicAdd(deadCount, 1u);   // return the slot to the dead list,
        deadList[slot] = i;                      // since the lifetime pass skips lifetime<=0
        return;
    }
    else if (dot(vel, n) < 0.0)
        particles[i].velocity = vec4(reflected, 0.0);

    particles[i].position.xyz = pos + n * k_separation;
}