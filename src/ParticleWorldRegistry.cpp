#include "ParticleWorldRegistry.h"
#include <algorithm>

void ParticleWorldRegistry::Register(ParticleWorld* world)
{
    if (!world) return;
    if (std::find(m_worlds.begin(), m_worlds.end(), world) == m_worlds.end())
        m_worlds.push_back(world);
}

void ParticleWorldRegistry::Unregister(ParticleWorld* world)
{
    m_worlds.erase(std::remove(m_worlds.begin(), m_worlds.end(), world), m_worlds.end());
}
