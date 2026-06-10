#pragma once
#include <cstdint>

namespace SimBindings
{
	// SSBO binding points
	// must match layout binding values in every compute shader
	
	constexpr uint32_t Particles = 0;
    constexpr uint32_t DeadList = 1;
    constexpr uint32_t DeadCount = 2;
    constexpr uint32_t Indirect = 3;
    constexpr uint32_t SortKeys = 4;
    constexpr uint32_t Emitters = 5;
    constexpr uint32_t SortKeysAlt = 6;
    constexpr uint32_t SortHistogram = 7;
    constexpr uint32_t SortCount = 8;       // alive entries compacted by keygen this frame
    constexpr uint32_t SortDispatch = 9;    // dispatch-indirect args for the radix passes

    // custom aux buffers in user stages must start here
    constexpr uint32_t UserBindingStart = 10;

    // UBO binding points

    constexpr uint32_t CameraUBO = 0;
    constexpr uint32_t WorldUBO = 1;

    // texture units
    constexpr uint32_t DepthTextureUnit = 0;
    constexpr uint32_t ParticleTextureUnit = 1;
    constexpr uint32_t LutColourUnit = 2;   // lifetime colour/alpha LUT (fragment)
    constexpr uint32_t LutScalarUnit = 3;   // lifetime scalar/size LUT (vertex)
}