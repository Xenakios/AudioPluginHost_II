#pragma once

#include "../Plugins/noise-plethora/plugins/NoisePlethoraPlugin.hpp"
#include "../Plugins/noise-plethora/plugins/Banks.hpp"
#include "audio/choc_AudioFileFormat_WAV.h"

class SignalSmoother
{
  public:
    SignalSmoother() {}
    inline double process(double in)
    {
        double result = in + m_slope * (m_history - in);
        m_history = result;
        return result;
    }
    void setSlope(double x) { m_slope = x; }
    double getSlope() const { return m_slope; }

  private:
    float m_history = 0.0f;
    float m_slope = 0.999f;
};

constexpr size_t ENVBLOCKSIZE = 64;

inline void list_plugins()
{
    int k = 0;
    for (int i = 0; i < numBanks; ++i)
    {
        auto &bank = getBankForIndex(i);
        std::cout << "bank " << i << "\n";
        for (int j = 0; j < programsPerBank; ++j)
        {
            std::cout << "\t" << bank.getProgramName(j) << "\t\t" << k << "\n";
            // availablePlugins[k] = bank.getProgramName(j);
            ++k;
        }
    }
}

class NoisePlethoraEngine
{
  public:
    NoisePlethoraEngine()
    {
        int k = 0;
        for (int i = 0; i < numBanks; ++i)
        {
            auto &bank = getBankForIndex(i);
            std::cout << "bank " << i << "\n";
            for (int j = 0; j < programsPerBank; ++j)
            {
                auto progname = bank.getProgramName(j);
                std::cout << "\t" << progname << "\t\t" << k << "\n";
                // availablePlugins[k] = bank.getProgramName(j);
                ++k;
                auto p = MyFactory::Instance()->Create(progname);
                m_plugs.push_back(std::move(p));
            }
        }
    }
    double hipasscutoff = 12.0;
    void processToFile(std::string filename, double durinseconds)
    {
        double sr = 44100.0;
        unsigned int numoutchans = 1;
        choc::audio::AudioFileProperties outfileprops;
        outfileprops.formatName = "WAV";
        outfileprops.bitDepth = choc::audio::BitDepth::float32;
        outfileprops.numChannels = numoutchans;
        outfileprops.sampleRate = sr;
        choc::audio::WAVAudioFileFormat<true> wavformat;
        auto writer = wavformat.createWriter(filename, outfileprops);
        if (!writer)
            throw std::runtime_error("Could not create output file");
        int outlen = durinseconds * sr;
        choc::buffer::ChannelArrayBuffer<float> buf{numoutchans, (unsigned int)outlen};
        buf.clear();
        for (auto &p : m_plugs)
        {
            p->init();
            p->m_sr = sr;
        }
        auto m_plug = m_plugs[0].get();
        auto chansdata = buf.getView().data.channels;
        StereoSimperSVF dcblocker;
        dcblocker.setCoeff(hipasscutoff, 0.01, 1.0 / sr);
        dcblocker.init();
        StereoSimperSVF filter;

        filter.init();
        int modcounter = 0;
        double p0 = 0.5;
        double p1 = 0.5;
        double mod_p0 = 0.0;
        double mod_p1 = 0.0;
        double filt_cut_off = 120.0;
        double filt_modulation = 0.0;
        double volume = -6.0;
        double volume_mod = 0.0;
        double filt_resonance = 0.01;
        double filt_res_mod = 0.0;
        ClapEventSequence::Iterator eviter(m_seq);
        eviter.setTime(0.0);
        m_gain_smoother.setSlope(0.999);
        for (int i = 0; i < outlen; ++i)
        {
            if (modcounter == 0)
            {
                auto evts = eviter.readNextEvents(ENVBLOCKSIZE / sr);
                double seconds = i / sr;
                for (auto &e : evts)
                {
                    if (e.event.header.type == CLAP_EVENT_PARAM_MOD)
                    {
                        auto pev = (const clap_event_param_mod *)&e.event;
                        if (pev->param_id == 0)
                        {
                            // std::cout << "mod param 0 " << pev->amount << "\n";
                            mod_p0 = pev->amount;
                        }
                        if (pev->param_id == 1)
                        {
                            mod_p1 = pev->amount;
                        }
                        if (pev->param_id == 2)
                        {
                            filt_modulation = pev->amount;
                        }
                        if (pev->param_id == 3)
                        {
                            volume_mod = pev->amount;
                        }
                        if (pev->param_id == 4)
                        {
                            filt_res_mod = pev->amount;
                        }
                    }
                    if (e.event.header.type == CLAP_EVENT_PARAM_VALUE)
                    {
                        auto pev = (const clap_event_param_value *)&e.event;
                        if (pev->param_id == 0)
                        {
                            std::cout << "set param 0 " << pev->value << "\n";
                            p0 = pev->value;
                        }
                        if (pev->param_id == 1)
                        {
                            p1 = pev->value;
                        }
                        if (pev->param_id == 2)
                        {
                            filt_cut_off = pev->value;
                        }
                        if (pev->param_id == 3)
                        {
                            volume = pev->value;
                        }
                        if (pev->param_id == 4)
                        {
                            filt_resonance = pev->value;
                        }
                        if (pev->param_id == 5)
                        {
                            int pindex = pev->value;
                            pindex = std::clamp(pindex, 0, (int)m_plugs.size());
                            m_plug = m_plugs[pindex].get();
                        }
                    }
                }
                double final_cutoff =
                    std::clamp<double>(filt_cut_off + filt_modulation, 0.0, 130.0);
                double final_reson = std::clamp<double>(filt_resonance + filt_res_mod, 0.01, 0.99);
                filter.setCoeff(final_cutoff, final_reson, 1.0 / sr);
            }
            ++modcounter;
            if (modcounter == ENVBLOCKSIZE)
                modcounter = 0;
            double finalp0 = std::clamp<double>(p0 + mod_p0, 0.0, 1.0);
            double finalp1 = std::clamp<double>(p1 + mod_p1, 0.0, 1.0);
            m_plug->process(finalp0, finalp1);
            double finalvolume = std::clamp<double>(volume + volume_mod, -96.0, 0.0);
            double gain = m_gain_smoother.process(xenakios::decibelsToGain(finalvolume));
            float outL = m_plug->processGraph() * gain;
            float outR = outL;
            dcblocker.step<StereoSimperSVF::HP>(dcblocker, outL, outR);
            filter.step<StereoSimperSVF::LP>(filter, outL, outR);
            outL = std::clamp(outL, -0.95f, 0.95f);
            chansdata[0][i] = outL;
        }
        if (!writer->appendFrames(buf.getView()))
            throw std::runtime_error("Could not write output file");
    }

    void setSequence(ClapEventSequence seq)
    {
        m_seq = seq;
        m_seq.sortEvents();
    }

  private:
    std::vector<std::unique_ptr<NoisePlethoraPlugin>> m_plugs;
    ClapEventSequence m_seq;
    SignalSmoother m_gain_smoother;
};
