#pragma once 
#include <cstdint>

struct SimContext {
	float deltaTime;
	float totalTime;
	uint32_t maxElements;
	uint32_t emitterCount;
	uint32_t spawnCount;
};