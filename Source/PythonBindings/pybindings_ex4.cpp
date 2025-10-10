#include <pybind11/pybind11.h>
#include <pybind11/buffer_info.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "../Common/xap_breakpoint_envelope.h"
#include "sst/basic-blocks/dsp/EllipticBlepOscillators.h"

namespace py = pybind11;

inline py::array_t<float> generate_tone(int tone_type, xenakios::Envelope &pitch_env,
                                        xenakios::Envelope &volume_env, double sr, double duration)
{
    tone_type = std::clamp(tone_type, 0, 3);
    pitch_env.sortPoints();
    volume_env.sortPoints();
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
    sst::basic_blocks::dsp::EBTri<> osc_tri;
    sst::basic_blocks::dsp::EBSaw<> osc_saw;
    sst::basic_blocks::dsp::EBPulse<> osc_pulse;
    osc_sin.setSampleRate(sr);
    osc_tri.setSampleRate(sr);
    osc_saw.setSampleRate(sr);
    osc_pulse.setSampleRate(sr);
    const size_t blocksize = 64;
    int framecount = 0;
    float *writebuf = output_audio.mutable_data(0);
    volume_env.clearOutputBlock();
    while (framecount < frames)
    {
        size_t framestoprocess = std::min<int>(blocksize, frames - framecount);
        double tpos = framecount / sr;
        double pitch = pitch_env.getValueAtPosition(tpos);
        pitch = std::clamp(pitch, 0.0, 136.0);
        double hz = 440.0 * std::pow(2.0, 1.0 / 12 * (pitch - 69.0));
        if (tone_type == 0)
            osc_sin.setFrequency(hz);
        else if (tone_type == 1)
            osc_tri.setFrequency(hz);
        else if (tone_type == 2)
            osc_saw.setFrequency(hz);
        else if (tone_type == 3)
            osc_pulse.setFrequency(hz);
        volume_env.processBlock(tpos, sr, 0, 64);
        for (int i = 0; i < framestoprocess; ++i)
        {
            float sample = 0.0f;
            if (tone_type == 0)
                sample = osc_sin.step();
            else if (tone_type == 1)
                sample = osc_tri.step();
            else if (tone_type == 2)
                sample = osc_saw.step();
            else if (tone_type == 3)
                sample = osc_pulse.step();
            // double gain = xenakios::decibelsToGain(volume_env.outputBlock[i]);
            double gain = volume_env.outputBlock[i];
            gain = std::clamp(gain, 0.0, 1.0);
            gain = gain * gain * gain;
            writebuf[framecount + i] = sample * gain;
        }
        framecount += blocksize;
    }
    return output_audio;
}

void init_py4(py::module_ &m, py::module_ &m_const) { m.def("generate_tone", &generate_tone); }
