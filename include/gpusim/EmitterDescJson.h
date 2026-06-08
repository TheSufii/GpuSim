#pragma once
#include "EmitterDesc.h"
#include <nlohmann/json.hpp>
#include <string>

inline nlohmann::json EmitterDescToJson(const EmitterDesc& d, const std::string& texture)
{
    using nlohmann::json;
    auto v3 = [](const glm::vec3& v) { return json::array({ v.x, v.y, v.z }); };
    auto v4 = [](const glm::vec4& v) { return json::array({ v.x, v.y, v.z, v.w }); };

    json colourStops = json::array();
    for (uint32_t i = 0; i < d.colourStopCount; ++i)
        colourStops.push_back({ {"t", d.colourOverLife[i].t},
                                {"rgba", v4(d.colourOverLife[i].colour)},
                                {"ease", (uint32_t)d.colourOverLife[i].ease} });

    json sizeKeys = json::array();
    for (uint32_t i = 0; i < d.sizeKeyCount; ++i)
        sizeKeys.push_back({ {"t", d.sizeOverLife[i].t},
                             {"v", d.sizeOverLife[i].value},
                             {"ease", (uint32_t)d.sizeOverLife[i].ease} });

    return json{
        {"version", 1},
        {"emissionMode", (uint32_t)d.emissionMode},
        {"spawnRate", d.spawnRate},
        {"burstCount", d.burstCount},
        {"burstInterval", d.burstInterval},
        {"startDelay", d.startDelay},
        {"duration", d.duration},
        {"emissionShape", (uint32_t)d.emissionShape},
        {"emissionShapeSize", v3(d.emissionShapeSize)},
        {"shapeDirection", d.shapeDirection},
        {"spread", d.spread},
        {"speed", json::array({ d.minSpeed, d.maxSpeed })},
        {"lifetime", json::array({ d.minLifetime, d.maxLifetime })},
        {"size", json::array({ d.minSize, d.maxSize })},
        {"drag", d.drag},
        {"noiseStrength", d.noiseStrength},
        {"noiseScale", d.noiseScale},
        {"noiseScrollSpeed", d.noiseScrollSpeed},
        {"colour", v4(d.colour)},
        {"blendMode", (uint32_t)d.blendMode},
        {"useCollision", d.useCollision},
        {"restitution", d.restitution},
        {"useAttractor", d.useAttractor},
        {"attractorStrength", d.attractorStrength},
        {"attractorOffset", v3(d.attractorOffset)},
        {"gravity", v3(d.gravity)},
        {"texture", texture},
        {"frameCount", d.frameCount},
        {"animMode", (uint32_t)d.animMode},
        {"animFps", d.animFps},
        {"velocityStretch", d.velocityStretch},
        {"active", d.active},
        {"colourOverLife", colourStops},
        {"sizeOverLife", sizeKeys},
    };
}

inline void EmitterDescFromJson(const nlohmann::json& j, EmitterDesc& d, std::string& texture)
{
    auto getv3 = [](const nlohmann::json& a, glm::vec3 def) {
        if (a.is_array() && a.size() >= 3) return glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
        return def;
    };
    auto getv4 = [](const nlohmann::json& a, glm::vec4 def) {
        if (a.is_array() && a.size() >= 4) return glm::vec4(a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>());
        return def;
    };
    auto range = [&](const char* k, float& lo, float& hi) {
        if (j.contains(k) && j[k].is_array() && j[k].size() >= 2) { lo = j[k][0].get<float>(); hi = j[k][1].get<float>(); }
    };

    d.emissionMode      = (EmissionMode)  j.value("emissionMode", 0u);
    d.spawnRate         = j.value("spawnRate", d.spawnRate);
    d.burstCount        = j.value("burstCount", d.burstCount);
    d.burstInterval     = j.value("burstInterval", d.burstInterval);
    d.startDelay        = j.value("startDelay", d.startDelay);
    d.duration          = j.value("duration", d.duration);
    d.emissionShape     = (EmissionShape) j.value("emissionShape", 0u);
    d.emissionShapeSize = getv3(j.value("emissionShapeSize", nlohmann::json()), d.emissionShapeSize);
    d.shapeDirection    = j.value("shapeDirection", d.shapeDirection);
    d.spread            = j.value("spread", d.spread);
    range("speed",    d.minSpeed,    d.maxSpeed);
    range("lifetime", d.minLifetime, d.maxLifetime);
    range("size",     d.minSize,     d.maxSize);
    d.drag              = j.value("drag", d.drag);
    d.noiseStrength     = j.value("noiseStrength", d.noiseStrength);
    d.noiseScale        = j.value("noiseScale", d.noiseScale);
    d.noiseScrollSpeed  = j.value("noiseScrollSpeed", d.noiseScrollSpeed);
    d.colour            = getv4(j.value("colour", nlohmann::json()), d.colour);
    d.blendMode         = (BlendMode) j.value("blendMode", 1u);
    d.useCollision      = j.value("useCollision", d.useCollision);
    d.restitution       = j.value("restitution", d.restitution);
    d.useAttractor      = j.value("useAttractor", d.useAttractor);
    d.attractorStrength = j.value("attractorStrength", d.attractorStrength);
    d.attractorOffset   = getv3(j.value("attractorOffset", nlohmann::json()), d.attractorOffset);
    d.gravity           = getv3(j.value("gravity", nlohmann::json()), d.gravity);
    texture             = j.value("texture", std::string());
    d.frameCount        = j.value("frameCount", d.frameCount);
    d.animMode          = (AnimMode) j.value("animMode", 0u);
    d.animFps           = j.value("animFps", d.animFps);
    d.velocityStretch   = j.value("velocityStretch", d.velocityStretch);
    d.active            = j.value("active", d.active);

    if (j.contains("colourOverLife") && j["colourOverLife"].is_array())
    {
        uint32_t n = 0;
        for (const auto& s : j["colourOverLife"])
        {
            if (n >= (uint32_t)MAX_GRADIENT_STOPS) break;
            d.colourOverLife[n].t      = s.value("t", 0.f);
            d.colourOverLife[n].colour = getv4(s.value("rgba", nlohmann::json()), glm::vec4(1.f));
            d.colourOverLife[n].ease   = (EaseMode) s.value("ease", 0u);
            ++n;
        }
        if (n > 0) d.colourStopCount = n;
    }
    if (j.contains("sizeOverLife") && j["sizeOverLife"].is_array())
    {
        uint32_t n = 0;
        for (const auto& s : j["sizeOverLife"])
        {
            if (n >= (uint32_t)MAX_CURVE_KEYS) break;
            d.sizeOverLife[n].t     = s.value("t", 0.f);
            d.sizeOverLife[n].value = s.value("v", 1.f);
            d.sizeOverLife[n].ease  = (EaseMode) s.value("ease", 0u);
            ++n;
        }
        if (n > 0) d.sizeKeyCount = n;
    }
}
