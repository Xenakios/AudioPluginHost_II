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

struct AutomationEvent
{
    double timestamp; // seconds/beats
    double value;     // parameter value
    uint32_t id;      // parameter id (compatible with clap uint32_t id)
    uint32_t flags;   // flags/extra data
    uint16_t target;  // target synth voice/channel/effect submodule
    bool operator<(const AutomationEvent &other) { return timestamp < other.timestamp; }
};

struct AutomationSequence
{
    std::vector<AutomationEvent> events;
    AutomationSequence() { events.reserve(128); }
    void add_event(double timestamp, uint32_t id, double value)
    {
        events.emplace_back(timestamp, value, id);
    }
    void clear() { events.clear(); }
    void sort_events() { choc::sorting::stable_sort(events.begin(), events.end()); }
    struct Iterator
    {
        /// Creates an iterator positioned at the start of the sequence.
        Iterator(const AutomationSequence &s, double sr) : owner(s), sampleRate(sr) {}
        Iterator(const Iterator &) = default;
        Iterator(Iterator &&) = default;

        /// Seeks the iterator to the given time

        void setTime(int64_t newTimeStamp)
        {
            auto eventData = owner.events.data();

            while (nextIndex != 0 &&
                   eventData[nextIndex - 1].timestamp * sampleRate >= newTimeStamp)
                --nextIndex;

            while (nextIndex < owner.events.size() &&
                   eventData[nextIndex].timestamp * sampleRate < newTimeStamp)
                ++nextIndex;

            currentTime = newTimeStamp;
        }

        /// Returns the current iterator time
        int64_t getTime() const noexcept { return currentTime; }

        /// Returns a set of events which lie between the current time, up to (but not
        /// including) the given duration. This function then increments the iterator to
        /// set its current time to the end of this block.

        choc::span<const AutomationEvent> readNextEvents(int duration)
        {
            auto start = nextIndex;
            auto eventData = owner.events.data();
            auto end = start;
            auto total = owner.events.size();
            auto endTime = currentTime + duration;
            currentTime = endTime;

            while (end < total && eventData[end].timestamp * sampleRate < endTime)
                ++end;

            nextIndex = end;

            return {eventData + start, eventData + end};
        }

      private:
        const AutomationSequence &owner;
        int64_t currentTime = 0;
        size_t nextIndex = 0;
        double sampleRate = 0.0;
    };
};

inline void gendyn_print_params(Gendyn2026 &g)
{
    for (const auto &p : g.paramMetaDatas)
    {
        std::print("{:12} {}/{} {}\n", p.id, p.groupName, p.name, p.defaultVal);
    }
}

py::array_t<float> gendyn_render(Gendyn2026 &gendyn, double sr, double outdur,
                                 AutomationSequence &automation)
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
    automation.sort_events();
    AutomationSequence::Iterator eviter{automation, sr};
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

            float val = ev.value;
            auto it = gendyn.parIdToValuePtr.find(ev.id);
            if (it != gendyn.parIdToValuePtr.end())
            {
                auto pmd = gendyn.parIdToMetaDataPtr[ev.id];
                float minv = pmd->minVal;
                float maxv = pmd->maxVal;
                val = std::clamp(val, minv, maxv);
                *it->second = val;
                if (ev.id == Gendyn2026::PAR_RANDSEED)
                    gendyn.rng.seed(val, 13);
                else if (ev.id == Gendyn2026::PAR_NUMSEGMENTS)
                {
                    gendyn.smoothed_num_nodes.setTarget(val);
                }
                else if (ev.id == Gendyn2026::PAR_TRIGRESET)
                    gendyn.reset();
                else if (ev.id == Gendyn2026::PAR_INTERPOLATIONMODE)
                    gendyn.setInterpolationMode(val);
            }
            else
            {
                if (ev.id == 100)
                    highpass.setCoeff(val, 0.0f, 1.0 / sr);
                else if (ev.id == 101)
                    lpcutoff = val;
                else if (ev.id == 102)
                    lpreso = val;
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
    py::class_<AutomationSequence>(m, "AutomationSequence")
        .def(py::init<>())
        .def("add_event", &AutomationSequence::add_event, "time"_a, "id"_a, "value"_a);
    py::class_<Gendyn2026>(m, "gendyn")
        .def(py::init<>())
        .def("print_params", &gendyn_print_params)
        .def("prepare", &Gendyn2026::prepare)
        .def("render", &gendyn_render, "samplerate"_a = 44100.0, "duration"_a = 1.0, "automation"_a);
}
