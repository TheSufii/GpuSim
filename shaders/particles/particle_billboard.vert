#version 450 core
#include "common.glsl"

layout(std430, binding = BIND_PARTICLES) readonly buffer ParticleBuf { Particle particles[]; };
layout(std430, binding = BIND_SORTKEYS)  readonly buffer SortBuf { SortEntry sortKeys[]; };
layout(std430, binding = BIND_EMITTERS)  readonly buffer EmitterBuf { EmitterData emitters[]; };

layout(binding = 3) uniform sampler2D u_lutScalar;   // size-over-life LUT (LutScalarUnit)

out vec2  v_uv;
out float v_lifeRatio;   // 0 = just born, 1 = dying
out float v_age;         // seconds alive (for looping animation)
out vec3  v_tint;        // per-particle colour multiplier (1 = neutral)
flat out uint v_emitter;

const vec2 CORNER[6] = vec2[](
    vec2(-0.5, 0.5), vec2(0.5, 0.5), vec2(0.5,-0.5),
    vec2(-0.5, 0.5), vec2(0.5,-0.5), vec2(-0.5,-0.5));
const vec2 UV[6] = vec2[](
    vec2(0,1), vec2(1,1), vec2(1,0),
    vec2(0,1), vec2(1,0), vec2(0,0));

void main()
{
    uint sorted = uint(gl_VertexID) / 6u;
    uint corner = uint(gl_VertexID) % 6u;

    uint pIndex = sortKeys[sorted].particleIndex;    //sorted back-to-front
    Particle p  = particles[pIndex];

    if (p.lifetime <= 0.0) { gl_Position = vec4(2.0); return; } 

    vec3 camRight = vec3(u_view[0][0], u_view[1][0], u_view[2][0]);
    vec3 camUp    = vec3(u_view[0][1], u_view[1][1], u_view[2][1]);

    float lifeRatio = 1.0 - (p.lifetime / max(p.maxLifetime, 1e-5));

    
    float vrow    = (float(p.emitterIndex) + 0.5) / float(MAX_EMITTERS);
    float sizeMul = texture(u_lutScalar, vec2(lifeRatio, vrow)).r;
    float sz      = p.size * sizeMul;

    
    EmitterData em = emitters[p.emitterIndex];
    vec3  axisR = camRight, axisU = camUp;
    float halfW = sz, halfH = sz;
    if (em.velocityStretch > 0.0)
    {
        vec2 vp = vec2(dot(p.velocity.xyz, camRight), dot(p.velocity.xyz, camUp)); // velocity in cam plane
        float speed = length(vp);
        if (speed > 1e-4)
        {
            vec2 d = vp / speed;
            axisU = camRight * d.x  + camUp * d.y;   // long axis follows velocity
            axisR = camRight * (-d.y) + camUp * d.x; // width axis perpendicular
            halfH = sz + speed * em.velocityStretch; // elongate by speed
        }
    }

    vec2 c = CORNER[corner];
    vec3 world = p.position.xyz + axisR * (c.x * halfW) + axisU * (c.y * halfH);

    gl_Position = u_proj * u_view * vec4(world, 1.0);
    v_uv        = UV[corner];
    v_lifeRatio = lifeRatio;
    v_age       = p.maxLifetime - p.lifetime;
   
    v_tint      = (p.position.w == 0.0) ? vec3(1.0)
                                        : unpackUnorm4x8(floatBitsToUint(p.position.w)).rgb * 2.0;
    v_emitter   = p.emitterIndex;

}