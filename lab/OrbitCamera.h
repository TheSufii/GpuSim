#pragma once
//
//  OrbitCamera — a tiny orbit/zoom camera for the lab. The library is camera-
//  agnostic (it just takes view/proj matrices via SetCamera), so the host owns
//  whatever camera it likes. This is the equivalent of the engine building a
//  CameraData from its CameraComponent — here we build it from mouse input.
//
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

struct OrbitCamera
{
    glm::vec3 target{ 0.f, 1.f, 0.f };
    float yaw      = 0.6f;
    float pitch    = 0.35f;
    float distance = 9.f;
    float fovY     = glm::radians(60.f);
    float nearP    = 0.1f;
    float farP     = 1000.f;

    void orbit(float dYaw, float dPitch)
    {
        yaw   += dYaw;
        pitch += dPitch;
        const float lim = glm::radians(89.f);
        pitch = std::clamp(pitch, -lim, lim);
    }
    void zoom(float dz) { distance = std::clamp(distance - dz, 0.5f, 200.f); }

    glm::vec3 eye() const
    {
        return target + distance * glm::vec3(
            std::cos(pitch) * std::sin(yaw),
            std::sin(pitch),
            std::cos(pitch) * std::cos(yaw));
    }
    glm::mat4 view() const { return glm::lookAt(eye(), target, glm::vec3(0, 1, 0)); }
    glm::mat4 proj(float aspect) const { return glm::perspective(fovY, aspect, nearP, farP); }
};
