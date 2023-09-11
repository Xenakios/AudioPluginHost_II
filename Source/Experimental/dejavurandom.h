#pragma once

#include <random>
#include <array>
#include <algorithm>

template <typename Type>
inline Type maprange(Type sourceValue, Type sourceRangeMin, Type sourceRangeMax,
                     Type targetRangeMin, Type targetRangeMax)
{
    return targetRangeMin + ((targetRangeMax - targetRangeMin) * (sourceValue - sourceRangeMin)) /
                                (sourceRangeMax - sourceRangeMin);
}

struct DejaVuRandom
{
    std::array<unsigned int, 256> m_state;
    std::minstd_rand0 m_rng;
    int m_loop_index = 0;
    int m_loop_len = 8;
    float m_deja_vu = 0.0;
    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};
    DejaVuRandom(unsigned int seed) : m_rng(seed)
    {
        for (int i = 0; i < m_state.size(); ++i)
            m_state[i] = m_rng();
    }
    unsigned int max() const { return m_rng.max(); }
    unsigned int min() const { return m_rng.min(); }
    unsigned int operator()() { return next(); }
    unsigned int next()
    {
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
                    m_state[m_loop_len - 1] = m_rng();
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
                    std::uniform_int_distribution<int> selectdist(0, m_loop_len - 1);
                    // int slot = selectdist(m_rng);
                    // m_state[slot] = m_rng();
                    m_loop_index = selectdist(m_rng);
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
