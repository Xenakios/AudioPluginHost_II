#include <pybind11/pybind11.h>
#include <pybind11/buffer_info.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "../Common/xap_breakpoint_envelope.h"
#include "sst/basic-blocks/dsp/EllipticBlepOscillators.h"

namespace py = pybind11;

inline py::array_t<float> generate_tone(xenakios::Envelope &pitch_env,
                                        xenakios::Envelope &volume_env, double sr, double duration)
{
    int chans = 1;
    int frames = duration * sr;
    py::buffer_info binfo(
        nullptr,                                /* Pointer to buffer */
        sizeof(float),                          /* Size of one scalar */
        py::format_descriptor<float>::format(), /* Python struct-style format descriptor */
        2,                                      /* Number of dimensions */
        {chans, frames},                        /* Buffer dimensions */
        {sizeof(float) * frames,                /* Strides (in bytes) for each index */
         sizeof(float)});
    py::array_t<float> output_audio{binfo};
    sst::basic_blocks::dsp::EBApproxSin<> osc_sin;
    osc_sin.setSampleRate(sr);

    const size_t blocksize = 64;
    int framecount = 0;
    float *writebuf = output_audio.mutable_data(0);
    volume_env.clearOutputBlock();
    while (framecount < frames - blocksize)
    {
        double tpos = framecount / sr;
        double pitch = pitch_env.getValueAtPosition(tpos);
        double hz = 440.0 * std::pow(2.0, 1.0 / 12 * (pitch - 69.0));
        osc_sin.setFrequency(hz);
        volume_env.processBlock(tpos, sr, 0, 64);
        for (int i = 0; i < blocksize; ++i)
        {
            float sample = osc_sin.step();
            double gain = xenakios::decibelsToGain(volume_env.outputBlock[i]);
            writebuf[framecount + i] = sample * gain;
        }
        framecount += blocksize;
    }
    return output_audio;
}

void init_py4(py::module_ &m, py::module_ &m_const) { m.def("generate_tone", &generate_tone); }
