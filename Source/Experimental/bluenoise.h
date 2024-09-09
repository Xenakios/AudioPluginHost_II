#pragma once

#include "xap_utils.h"
#include <algorithm>
#include <random>

namespace xenakios
{
// Noise that tends to produce values as far as possible from the current value
// especially with higher depths
class BlueNoise
{
  public:
    BlueNoise(unsigned int seed = 0) : m_rng{seed, 1} { m_previous = m_dist(m_rng); }
    float operator()()
    {
        float maxdist = 0.0f;
        float z0 = 0.0f;
        for (int i = 0; i < m_depth; ++i)
        {
            float z1 = m_dist(m_rng);
            float dist = std::abs(z1 - m_previous);
            if (dist > maxdist)
            {
                maxdist = dist;
                z0 = z1;
            }
        }
        m_previous = z0;
        return m_previous;
    }

    void setDepth(int d) { m_depth = std::clamp(d, 1, 32); }
    int getDepth() const { return m_depth; }

  private:
    xenakios::Xoroshiro128Plus m_rng;
    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};
    float m_previous = 0.0f;
    int m_depth = 4;
};

} // namespace xenakios
