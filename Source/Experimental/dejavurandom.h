#pragma once

#include <random>
#include <array>
#include <algorithm>
#include "xap_utils.h"

template <typename Type>
inline Type maprange(Type sourceValue, Type sourceRangeMin, Type sourceRangeMax,
                     Type targetRangeMin, Type targetRangeMax)
{
    return targetRangeMin + ((targetRangeMax - targetRangeMin) * (sourceValue - sourceRangeMin)) /
                                (sourceRangeMax - sourceRangeMin);
}
namespace xenakios
{
/*
This should be cleaned up a bit...
Random number generator with a history that can repeat or shuffle
*/
class DejaVuRandom
{
  private:
    static constexpr size_t maxSlots = 256;
    std::array<float, maxSlots> m_state;
    using UnderlyingEngine = xenakios::Xoroshiro128Plus;
    UnderlyingEngine m_rng;
    int m_loop_index = 0;
    int m_loop_len = 8;
    float m_deja_vu = 0.0;
    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};
    bool m_deja_vu_enabled = true;

  public:
    DejaVuRandom(unsigned int seed) : m_rng{seed, 65537}
    {
        for (int i = 0; i < m_state.size(); ++i)
            m_state[i] = m_dist(m_rng);
    }
    void setLoopLength(int len) { m_loop_len = std::clamp(len, 1, (int)maxSlots); }
    void setDejaVu(float dv) { m_deja_vu = std::clamp(dv, 0.0f, 1.0f); }
    void setDejaVuEnabled(bool b) { m_deja_vu_enabled = b; }
    float nextFloatInRange(float minv, float maxv)
    {
        return maprange(nextFloat(), 0.0f, 1.0f, minv, maxv);
    }
    int nextIntInRange(int minv, int maxv)
    {
        int r = std::round(maprange<float>(nextFloat(), 0.0, 1.0f, minv, maxv));
        assert(r >= minv && r <= maxv);
        return r;
    }
    float nextFloat()
    {
        if (!m_deja_vu_enabled)
        {
            return m_dist(m_rng);
        }
        auto next = m_state[m_loop_index];
        bool rewinded = false;
        ++m_loop_index;
        if (m_loop_index >= m_loop_len)
        {
            m_loop_index = 0;
            rewinded = true;
        }
        // if (rewinded)
        {
            if (m_deja_vu >= 0.0f && m_deja_vu <= 0.5f)
            {
                float prob = maprange(m_deja_vu, 0.0f, 0.5f, 0.0f, 1.0f);
                if (m_dist(m_rng) >= prob)
                {
                    // rotate state left and generate new random number to end of loop
                    std::rotate(m_state.begin(), m_state.begin() + 1, m_state.begin() + m_loop_len);
                    assert((m_loop_len - 1) >= 0);
                    m_state[m_loop_len - 1] = m_dist(m_rng);
                    --m_loop_index;
                    if (m_loop_index < 0)
                        m_loop_index = m_loop_len - 1;
                }
            }
            else
            // (m_deja_vu > 0.5f) // && m_deja_vu < 0.9f)
            {
                float prob = maprange(m_deja_vu, 0.5f, 1.0f, 1.0f, 0.0f);
                if (m_dist(m_rng) >= prob)
                {
                    m_loop_index = mapvalue<float>(m_dist(m_rng), 0.0f, 1.0f, 0, m_loop_len - 1);
                }
            }
            /*
            else
            {
                std::shuffle(m_state.begin(), m_state.begin() + m_loop_len, m_rng);
            }
            */
        }
        return next;
    }
};
} // namespace xenakios
