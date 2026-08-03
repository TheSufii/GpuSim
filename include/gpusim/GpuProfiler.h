#pragma once
#include <glad/gl.h>
#include <cstdint>
#include <string>
#include <vector>



// Begin/End bracket the GL commands of one stage; results are polled a few
// frames later so reading them never stalls the pipeline. Each stage keeps a
// small ring of query objects (one per in-flight frame). GetTimings() returns
// the most recently completed time for every stage, in first-seen order.


class GpuProfiler
{
public:
    struct Timing { std::string name; float ms; };

    GpuProfiler() = default;
    GpuProfiler(const GpuProfiler&) = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;
    GpuProfiler(GpuProfiler&&) noexcept = default;
    GpuProfiler& operator=(GpuProfiler&&) noexcept = default;

    ~GpuProfiler()
    {
        for (Stage& s : m_stages)
            for (Sample& smp : s.ring)
                if (smp.query) glDeleteQueries(1, &smp.query);
    }

    void SetEnabled(bool e) { m_enabled = e; }
    bool IsEnabled() const  { return m_enabled; }

    // once per frame, before the first Begin
    void BeginFrame()
    {
        if (!m_enabled) return;
        m_ring = (m_ring + 1) % k_ringSize;
        Poll();
    }

    void Begin(const std::string& name)
    {
        if (!m_enabled) return;
        Stage& s = FindOrCreate(name);
        Sample& smp = s.ring[m_ring];
        if (smp.inFlight) return;            // GPU > k_ringSize frames behind; skip this frame
        if (!smp.query) glGenQueries(1, &smp.query);
        glBeginQuery(GL_TIME_ELAPSED, smp.query);
        m_active = &s;
    }

    void End()
    {
        if (!m_enabled || !m_active) return;
        glEndQuery(GL_TIME_ELAPSED);
        m_active->ring[m_ring].inFlight = true;
        m_active = nullptr;
    }

    std::vector<Timing> GetTimings() const
    {
        std::vector<Timing> out;
        out.reserve(m_stages.size());
        for (const Stage& s : m_stages) out.push_back({ s.name, s.lastMs });
        return out;
    }

private:
    static constexpr int k_ringSize = 4;

    struct Sample { GLuint query = 0; bool inFlight = false; };
    struct Stage  { std::string name; Sample ring[k_ringSize]; float lastMs = 0.f; };

    // harvest any finished queries without blocking
    void Poll()
    {
        for (Stage& s : m_stages)
            for (Sample& smp : s.ring)
            {
                if (!smp.inFlight) continue;
                GLuint avail = 0;
                glGetQueryObjectuiv(smp.query, GL_QUERY_RESULT_AVAILABLE, &avail);
                if (!avail) continue;
                GLuint64 ns = 0;
                glGetQueryObjectui64v(smp.query, GL_QUERY_RESULT, &ns);
                s.lastMs = float(double(ns) * 1e-6);
                smp.inFlight = false;
            }
    }

    Stage& FindOrCreate(const std::string& name)
    {
        for (Stage& s : m_stages)
            if (s.name == name) return s;
        m_stages.push_back(Stage{ name });
        return m_stages.back();
    }

    std::vector<Stage> m_stages;
    Stage* m_active = nullptr;     // open Begin, cleared by End before any reallocation
    int    m_ring = 0;
    bool   m_enabled = true;
};
