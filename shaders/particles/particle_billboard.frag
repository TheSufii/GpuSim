#version 450 core
#include "common.glsl"

layout(std430, binding = BIND_EMITTERS) readonly buffer EmitterBuf { EmitterData emitters[]; };

layout(binding = 0) uniform sampler2D      u_depthBuffer;    // DepthTextureUnit
layout(binding = 1) uniform sampler2DArray u_particleTex;    // ParticleTextureUnit
layout(binding = 2) uniform sampler2D      u_lutColour;      // colour+alpha over life (LutColourUnit)

uniform uint  u_hasTextureArray;
uniform uint  u_softEnabled;
uniform float u_near;
uniform float u_far;
uniform float u_softFadeDist;

in vec2  v_uv;
in float v_lifeRatio;
in float v_age;
flat in uint v_emitter;

out vec4 fragColour;

float linearize(float d)   // depth-buffer [0,1] -> positive view distance
{
    float z = d * 2.0 - 1.0;
    return (2.0 * u_near * u_far) / (u_far + u_near - z * (u_far - u_near));
}

void main()
{

    EmitterData em = emitters[v_emitter];

    vec4 src;
    if (u_hasTextureArray == 1u && em.textureIndex != NO_TEXTURE)
    {
        // each animation frame is its own full-resolution array layer,
        // stored contiguously starting at textureIndex (frameCount 1 = static).
        float frames = float(max(em.frameCount, 1u));
        float fidx = (em.animMode == ANIM_LOOP)
            ? floor(mod(v_age * em.animFps, frames))
            : floor(clamp(v_lifeRatio, 0.0, 0.9999) * frames);

        src = texture(u_particleTex, vec3(v_uv, float(em.textureIndex) + fidx));
    }
    else
    {
        float d = length(v_uv - 0.5) * 2.0;          // procedural soft circle
        src = vec4(1.0, 1.0, 1.0, 1.0 - smoothstep(0.8, 1.0, d));
    }

    // colour + alpha over life from this emitter's LUT row; em.colour is an overall multiplier.
    float vrow = (float(v_emitter) + 0.5) / float(MAX_EMITTERS);
    vec4  grad = texture(u_lutColour, vec2(v_lifeRatio, vrow));
    vec4  col  = src * grad * em.colour;

    if (u_softEnabled == 1u)
    {
        vec2 suv = gl_FragCoord.xy / vec2(textureSize(u_depthBuffer, 0));
        float scene = linearize(texture(u_depthBuffer, suv).r);
        float frag  = linearize(gl_FragCoord.z);
        col.a *= clamp((scene - frag) / max(u_softFadeDist, 1e-4), 0.0, 1.0);
    }

    if (col.a < 0.002) discard;

    // premultiplied alpha. blend func is GL_ONE, GL_ONE_MINUS_SRC_ALPHA.
    //   alpha: (rgb*a, a)  -> standard over
    //   additive: (rgb*a, 0) -> pure add, occludes nothing, order-independent
    vec3 premul = col.rgb * col.a;
    float outA = (em.blendMode == BLEND_ADDITIVE) ? 0.0 : col.a;
    fragColour = vec4(premul, outA);
}