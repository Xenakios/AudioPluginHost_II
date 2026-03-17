#include "gendyn.h"
#include "../Common/clap_eventsequence.h"
#include "../Common/xapdsp.h"
#include <print>
#include <pybind11/pybind11.h>
#include <pybind11/buffer_info.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "../cli/xcli_utils.h"

namespace py = pybind11;

inline void gendyn_print_params(Gendyn2026 &g)
{
    for (const auto &p : g.paramMetaDatas)
    {
        std::print("{:12} {}/{} {}\n", p.id, p.groupName, p.name, p.defaultVal);
    }
}

py::array_t<float> gendyn_render(Gendyn2026 &gendyn, double sr, double outdur,
                                 ClapEventSequence &events)
{
    int numOutChans = 1;
    int frames = outdur * sr;
    py::buffer_info binfo(
        nullptr,                                /* Pointer to buffer */
        sizeof(float),                          /* Size of one scalar */
        py::format_descriptor<float>::format(), /* Python struct-style format descriptor */
        2,                                      /* Number of dimensions */
        {numOutChans, frames},                  /* Buffer dimensions */
        {sizeof(float) * frames,                /* Strides (in bytes) for each index */
         sizeof(float)});
    py::array_t<float> output_audio{binfo};
    float *outdata = output_audio.mutable_data(0);
    int outcounter = 0;
    const size_t blocksize = 64;
    constexpr size_t osfactor = 2;
    dsp::Decimator<osfactor, 16> decimator;
    decimator.reset();
    gendyn.prepare(sr * osfactor);
    float osbuffer[osfactor];
    events.sortEvents();
    ClapEventSequence::IteratorSampleTime eviter{events, sr};
    StereoSimperSVF highpass;
    highpass.init();
    highpass.setCoeff(12.0, 0.0f, 1.0 / sr);
    StereoSimperSVF lowpass;
    lowpass.init();
    float lpreso = 0.0f;
    float lpcutoff = 135.0f; // ~19khz
    lowpass.setCoeff(lpcutoff, lpreso, 1.0 / sr);

    while (outcounter < frames)
    {
        size_t torender = std::min(blocksize, (size_t)frames - outcounter);
        auto blockevents = eviter.readNextEvents(blocksize);
        for (auto &ev : blockevents)
        {
            if (ev.event.header.type == CLAP_EVENT_PARAM_VALUE)
            {
                float val = ev.event.param.value;
                auto it = gendyn.parIdToValuePtr.find(ev.event.param.param_id);
                if (it != gendyn.parIdToValuePtr.end())
                {
                    auto pmd = gendyn.parIdToMetaDataPtr[ev.event.param.param_id];
                    float minv = pmd->minVal;
                    float maxv = pmd->maxVal;
                    val = std::clamp(val, minv, maxv);
                    *it->second = val;
                    if (ev.event.param.param_id == Gendyn2026::PAR_RANDSEED)
                        gendyn.rng.seed(val, 13);
                    else if (ev.event.param.param_id == Gendyn2026::PAR_NUMSEGMENTS)
                    {
                        gendyn.smoothed_num_nodes.setTarget(val);
                    }
                    else if (ev.event.param.param_id == Gendyn2026::PAR_TRIGRESET)
                        gendyn.reset();
                    else if (ev.event.param.param_id == Gendyn2026::PAR_INTERPOLATIONMODE)
                        gendyn.setInterpolationMode(val);
                }
                else
                {
                    if (ev.event.param.param_id == 100)
                        highpass.setCoeff(val, 0.0f, 1.0 / sr);
                    else if (ev.event.param.param_id == 101)
                        lpcutoff = val;
                    else if (ev.event.param.param_id == 102)
                        lpreso = val;
                }
            }
        }
        lowpass.setCoeff(lpcutoff, lpreso, 1.0 / sr);
        for (int i = 0; i < torender; ++i)
        {
            for (int j = 0; j < osfactor; ++j)
            {
                osbuffer[j] = gendyn.step();
            }
            float decimsample = decimator.process(osbuffer);
            float dummysample = 0.0f;
            StereoSimperSVF::step<StereoSimperSVF::HP>(highpass, decimsample, dummysample);
            StereoSimperSVF::step<StereoSimperSVF::LP>(lowpass, decimsample, dummysample);
            outdata[outcounter + i] = decimsample;
        }
        outcounter += blocksize;
    }

    return output_audio;
}

void init_py_gendyn(py::module_ &m, py::module_ &m_const)
{
    using namespace pybind11::literals;
    py::class_<Gendyn2026>(m, "gendyn")
        .def(py::init<>())
        .def("print_params", &gendyn_print_params)
        .def("prepare", &Gendyn2026::prepare)
        .def("render", &gendyn_render, "samplerate"_a = 44100.0, "duration"_a = 1.0, "events"_a);
}
