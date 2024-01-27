#include <pybind11/pybind11.h>
#include <vector>
#include <optional>
#include <pybind11/stl.h>
#include "../Plugins/noise-plethora/plugins/NoisePlethoraPlugin.hpp"
#include "../Plugins/noise-plethora/plugins/Banks.hpp"
#include <iostream>
#include "audio/choc_AudioFileFormat_WAV.h"
#include "xapdsp.h"
#include "xap_utils.h"
#include "xaps/clap_xaudioprocessor.h"

int add(int i, int j) { return i + j; }

std::optional<double> avg(std::vector<double> &v)
{
    if (v.size() == 0)
        return {};
    double sum = 0.0;
    for (auto &e : v)
        sum += e;
    return sum / v.size();
}

void list_plugins()
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

constexpr size_t ENVBLOCKSIZE = 64;

class NoisePlethoraEngine
{
  public:
    NoisePlethoraEngine(std::string plugintouse)
    {
        m_plug = MyFactory::Instance()->Create(plugintouse);
        if (!m_plug)
            throw std::runtime_error("Plugin could not be created");
    }
    double hipasscutoff = 12.0;
    void processToFile(std::string filename, double durinseconds, double p0, double p1)
    {
        m_p0_env.sortPoints();
        m_p1_env.sortPoints();
        m_filt_env.sortPoints();
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
        m_plug->init();
        m_plug->m_sr = sr;
        auto chansdata = buf.getView().data.channels;
        StereoSimperSVF dcblocker;
        dcblocker.setCoeff(hipasscutoff, 0.01, 1.0 / sr);
        dcblocker.init();
        StereoSimperSVF filter;

        filter.init();
        int modcounter = 0;
        double mod_p0 = 0.0;
        double mod_p1 = 0.0;
        for (int i = 0; i < outlen; ++i)
        {
            if (modcounter == 0)
            {
                double seconds = i / sr;
                m_p0_env.processBlock(seconds, sr, 2);
                m_p1_env.processBlock(seconds, sr, 2);
                m_filt_env.processBlock(seconds, sr, 2);
                mod_p0 = m_p0_env.outputBlock[0];
                mod_p1 = m_p1_env.outputBlock[0];
                filter.setCoeff(m_filt_env.outputBlock[0], 0.01, 1.0 / sr);
            }
            ++modcounter;
            if (modcounter == ENVBLOCKSIZE)
                modcounter = 0;
            double finalp0 = std::clamp<double>(p0 + mod_p0, 0.0, 1.0);
            double finalp1 = std::clamp<double>(p1 + mod_p1, 0.0, 1.0);
            m_plug->process(finalp0, finalp1);
            float outL = m_plug->processGraph() * 0.5;
            float outR = outL;
            dcblocker.step<StereoSimperSVF::HP>(dcblocker, outL, outR);
            filter.step<StereoSimperSVF::LP>(filter, outL, outR);
            chansdata[0][i] = outL;
        }
        if (!writer->appendFrames(buf.getView()))
            throw std::runtime_error("Could not write output file");
    }
    void setEnvelope(size_t index, xenakios::Envelope<ENVBLOCKSIZE> env)
    {
        if (index == 0)
            m_p0_env = env;
        if (index == 1)
            m_p1_env = env;
        if (index == 2)
            m_filt_env = env;
    }

  private:
    std::unique_ptr<NoisePlethoraPlugin> m_plug;
    using ENVTYPE = xenakios::Envelope<ENVBLOCKSIZE>;
    xenakios::Envelope<ENVBLOCKSIZE> m_p0_env{0.0};
    xenakios::Envelope<ENVBLOCKSIZE> m_p1_env{0.0};
    xenakios::Envelope<ENVBLOCKSIZE> m_filt_env{120.0};
    std::array<ENVTYPE, 3> m_envs{ENVTYPE{0.0}, ENVTYPE{0.0}, ENVTYPE{120.0}};
};

class ClapProcessingEngine
{
    std::unique_ptr<ClapPluginFormatProcessor> m_plug;

  public:
    ClapProcessingEngine(std::string plugfilename, int plugindex)
    {
        m_plug = std::make_unique<ClapPluginFormatProcessor>(plugfilename, plugindex);
        if (m_plug)
        {
            clap_plugin_descriptor desc;
            if (m_plug->getDescriptor(&desc))
            {
                std::cout << "created : " << desc.name << "\n";
            }
        }
    }
};

namespace py = pybind11;

PYBIND11_MODULE(xenakios, m)
{
    m.doc() = "pybind11 xenakios plugin"; // optional module docstring

    m.def("add", &add, "A function that adds two numbers");
    m.def("avg", &avg, "average of list");
    m.def("list_plugins", &list_plugins, "print noise plethora plugins");
    py::class_<ClapProcessingEngine>(m, "ClapEngine")
        .def(py::init<const std::string &,int>());
    py::class_<NoisePlethoraEngine>(m, "NoisePlethoraEngine")
        .def(py::init<const std::string &>())
        .def("processToFile", &NoisePlethoraEngine::processToFile)
        .def_readwrite("highpass", &NoisePlethoraEngine::hipasscutoff,
                       "high pass filter cutoff, in semitones")
        .def("setEnvelope", &NoisePlethoraEngine::setEnvelope);
    py::class_<xenakios::EnvelopePoint>(m, "EnvelopePoint").def(py::init<double, double>());
    py::class_<xenakios::Envelope<ENVBLOCKSIZE>>(m, "Envelope")
        .def(py::init<>())
        .def(py::init<std::vector<xenakios::EnvelopePoint>>())
        .def("numPoints", &xenakios::Envelope<ENVBLOCKSIZE>::getNumPoints)
        .def("addPoint", &xenakios::Envelope<ENVBLOCKSIZE>::addPoint);
}
