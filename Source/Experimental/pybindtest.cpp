#include <pybind11/pybind11.h>
#include <vector>
#include <optional>
#include <pybind11/stl.h>
#include "../Plugins/noise-plethora/plugins/NoisePlethoraPlugin.hpp"
#include "../Plugins/noise-plethora/plugins/Banks.hpp"
#include <iostream>
#include "audio/choc_AudioFileFormat_WAV.h"
#include "xapdsp.h"

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

class NoisePlethoraEngine
{
  public:
    NoisePlethoraEngine(std::string plugintouse)
    {
        m_plug = MyFactory::Instance()->Create(plugintouse);
        if (!m_plug)
            throw std::runtime_error("Plugin could not be created");
    }
    void processToFile(std::string filename, double durinseconds, double p0, double p1)
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
        m_plug->init();
        m_plug->m_sr = sr;
        auto chansdata = buf.getView().data.channels;
        StereoSimperSVF dcblocker;
        dcblocker.setCoeff(12.0, 0.01, 1.0 / sr);
        dcblocker.init();
        for (int i = 0; i < outlen; ++i)
        {
            m_plug->process(p0, p1);
            float outL = m_plug->processGraph() * 0.5;
            float outR = outL;
            dcblocker.step<StereoSimperSVF::HP>(dcblocker, outL, outR);
            chansdata[0][i] = outL;
        }
        writer->appendFrames(buf.getView());
    }

  private:
    std::unique_ptr<NoisePlethoraPlugin> m_plug;
};

namespace py = pybind11;


PYBIND11_MODULE(xenakios, m)
{
    m.doc() = "pybind11 example plugin"; // optional module docstring

    m.def("add", &add, "A function that adds two numbers");
    m.def("avg", &avg, "average of list");
    m.def("list_plugins", &list_plugins, "print noise plethora plugins");
    py::class_<NoisePlethoraEngine>(m, "NoisePlethoraEngine")
        .def(py::init<const std::string &>())
        .def("process_to_file", &NoisePlethoraEngine::processToFile);
}
