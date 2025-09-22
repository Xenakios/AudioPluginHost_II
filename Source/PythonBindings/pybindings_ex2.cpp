#include <pybind11/pybind11.h>
#include <pybind11/buffer_info.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <iostream>
#include "libMTSMaster.h"

#include "Tunings.h"
#include <print>

namespace py = pybind11;

class MTSESPSource
{
  public:
    MTSESPSource()
    {
        if (MTS_ShouldUpdateLibrary())
        {
            throw std::runtime_error(
                "MTS-ESP system library is out of date, you should update it!\n");
        }
        if (MTS_CanRegisterMaster())
        {
            MTS_RegisterMaster();
            std::cout << "registered MTS Source\n";
            double freqs[128];
            for (int i = 0; i < 128; ++i)
            {
                freqs[i] = 440.0 * std::pow(2.0, (69 - i) / 12.0);
            }
            MTS_SetNoteTunings(freqs);
            MTS_SetScaleName("12tet 440Hz");
        }
    }
    ~MTSESPSource()
    {
        MTS_DeregisterMaster();
        std::cout << "deregistered MTS Source\n";
    }

    void setNoteTuning(int midikey, double hz)
    {
        if (midikey >= 0 && midikey < 128)
        {
            MTS_SetNoteTuning(hz, midikey);
        }
    }
    void setNoteTuningMap(std::unordered_map<int, double> tunmap)
    {
        for (const auto &e : tunmap)
        {
            if (e.first >= 0 && e.first < 128)
            {
                if (e.second >= 0.0 && e.second < 20000.0)
                {
                    MTS_SetNoteTuning(e.second, e.first);
                }
            }
        }
    }

  private:
};

void init_py2(py::module_ &m, py::module_ &m_const)
{
    
    py::class_<MTSESPSource>(m, "MTS_Source")
        .def(py::init<>())
        .def("set_note_tuning_map", &MTSESPSource::setNoteTuningMap)
        .def("set_note_tuning", &MTSESPSource::setNoteTuning);
    m_const.attr("MIDI_0_FREQ") = Tunings::MIDI_0_FREQ;
    m.def("arraytest_out", []() {
        std::array<double, 16> result;
        for (int i = 0; i < result.size(); ++i)
            result[i] = 1.0 / result.size() * i;
        return result;
    });
    // m.def("arraytest_in", [](std::array<double, 4> arr) { std::print("{}", arr); });
}
