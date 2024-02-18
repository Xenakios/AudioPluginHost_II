#include <pybind11/pybind11.h>
#include <vector>
#include <optional>
#include <pybind11/stl.h>

#include <iostream>
#include "audio/choc_AudioFileFormat_WAV.h"
#include "xapdsp.h"
#include "xap_utils.h"
#include "offlineclaphost.h"
#include "dejavurandom.h"
#include "noiseplethoraengine.h"

namespace py = pybind11;

PYBIND11_MODULE(xenakios, m)
{
    m.doc() = "pybind11 xenakios plugin"; // optional module docstring

    m.def("list_plugins", &list_plugins, "print noise plethora plugins");

    py::class_<ClapEventSequence>(m, "ClapSequence")
        .def(py::init<>())
        .def("getNumEvents", &ClapEventSequence::getNumEvents)
        .def("getSizeInBytes", &ClapEventSequence::getApproxSizeInBytes)
        .def("addNoteOn", &ClapEventSequence::addNoteOn)
        .def("addNoteOff", &ClapEventSequence::addNoteOff)
        .def("addNote", &ClapEventSequence::addNote)
        .def("addParameterEvent", &ClapEventSequence::addParameterEvent)
        .def("addProgramChange", &ClapEventSequence::addProgramChange)
        .def("addNoteExpression", &ClapEventSequence::addNoteExpression);

    py::module m_const = m.def_submodule("constants", "Constants");

#define C(x) m_const.attr(#x) = py::int_((int)(x));
    C(CLAP_NOTE_EXPRESSION_TUNING);
    C(CLAP_NOTE_EXPRESSION_PAN);
    C(CLAP_NOTE_EXPRESSION_VOLUME);
    C(CLAP_NOTE_EXPRESSION_VIBRATO);
    C(CLAP_NOTE_EXPRESSION_BRIGHTNESS);
    C(CLAP_NOTE_EXPRESSION_PRESSURE);
    C(CLAP_NOTE_EXPRESSION_EXPRESSION);

    py::class_<DejaVuRandom>(m, "DejaVuRandom")
        .def(py::init<unsigned int>())
        .def("setLoopLength", &DejaVuRandom::setLoopLength)
        .def("setDejaVu", &DejaVuRandom::setDejaVu)
        .def("nextFloat", &DejaVuRandom::nextFloatInRange)
        .def("nextInt", &DejaVuRandom::nextIntInRange);

    py::class_<ClapProcessingEngine>(m, "ClapEngine")
        .def(py::init<const std::string &, int>())
        .def("setSequence", &ClapProcessingEngine::setSequencer)
        .def("getParameters", &ClapProcessingEngine::getParameters)
        .def("getNumParameters",&ClapProcessingEngine::getNumParameters)
        .def("getParameterInfoString",&ClapProcessingEngine::getParameterInfoString)
        .def("showGUIBlocking", &ClapProcessingEngine::openPluginGUIBlocking)
        .def("openWindow", &ClapProcessingEngine::openPersistentWindow)
        .def("saveStateToFile", &ClapProcessingEngine::saveStateToFile)
        .def("processToFile", &ClapProcessingEngine::processToFile);

    py::class_<NoisePlethoraEngine>(m, "NoisePlethoraEngine")
        .def(py::init<>())
        .def("processToFile", &NoisePlethoraEngine::processToFile)
        .def_readwrite("highpass", &NoisePlethoraEngine::hipasscutoff,
                       "high pass filter cutoff, in semitones")
        .def("setSequence", &NoisePlethoraEngine::setSequence);

    py::class_<xenakios::EnvelopePoint>(m, "EnvelopePoint")
        .def(py::init<double, double>())
        .def("getX", &xenakios::EnvelopePoint::getX)
        .def("getY", &xenakios::EnvelopePoint::getY);

    py::class_<xenakios::Envelope<ENVBLOCKSIZE>>(m, "Envelope")
        .def(py::init<>())
        .def(py::init<std::vector<xenakios::EnvelopePoint>>())
        .def("numPoints", &xenakios::Envelope<ENVBLOCKSIZE>::getNumPoints)
        .def("addPoint", &xenakios::Envelope<ENVBLOCKSIZE>::addPoint)
        .def("getPoint", &xenakios::Envelope<ENVBLOCKSIZE>::getPointSafe)
        .def("getValueAtPosition", &xenakios::Envelope<ENVBLOCKSIZE>::getValueAtPosition);

    py::class_<MultiModulator>(m, "MultiModulator")
        .def(py::init<double>())
        .def("applyToSequence", &MultiModulator::applyToSequence)
        .def("setOutputAsParameter", &MultiModulator::setOutputAsParameter)
        .def("setOutputAsParameterModulation", &MultiModulator::setOutputAsParameterModulation)
        .def("setConnection", &MultiModulator::setConnection)
        .def("setLFOProps", &MultiModulator::setLFOProps);

    m.def("generateNoteExpressionsFromEnvelope", &generateNoteExpressionsFromEnvelope, "",
          py::arg("targetSequence"), py::arg("sourceEnvelope"), py::arg("eventsStartTime"),
          py::arg("duration"), py::arg("granularity"), py::arg("noteExpressionType"),
          py::arg("port"), py::arg("channel"), py::arg("key"), py::arg("note_id"));
    m.def("generateParameterEventsFromEnvelope", &generateParameterEventsFromEnvelope);
    m.def("generateEnvelopeFromLFO", &generateEnvelopeFromLFO);
}
