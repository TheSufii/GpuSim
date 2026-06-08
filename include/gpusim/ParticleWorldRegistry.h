#pragma once
#include <vector>

class ParticleWorld;

// Lightweight, non-owning registry of live ParticleWorlds.
//
// Worlds register themselves on setup and unregister on destruction, so anything
// holding a reference to the registry (the editor today, a standalone GPUSim tool
// tomorrow) can enumerate whatever worlds currently exist without owning them.w
class ParticleWorldRegistry
{
public:
    void Register(ParticleWorld* world);
    void Unregister(ParticleWorld* world);

    const std::vector<ParticleWorld*>& Worlds() const { return m_worlds; }
    size_t Count() const { return m_worlds.size(); }

private:
    std::vector<ParticleWorld*> m_worlds;
};
