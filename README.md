# AudioPluginHost Mark II

Fork of the JUCE AudioPluginHost, with some improvements.

At the moment, the only difference is the added internal plugin for playing 
audio files, which is a work in progress. That can also be built as a separate plugin
for use in other hosts.

Future ideas that may or may not ever be implemented :

- CPU meter, possibly for individual plugins too
- Input/Output level meters
- Implement tempo and audio playhead so plugins that depend on that can be used
- Record live output into a file
- Offline render
- Some super simple way to generate MIDI for testing instrument plugins
- Plugin parameter automation/modulation
- Use Tracktion Graph instead of the Juce AudioProcessorGraph

To build, you need cmake, JUCE at the repo root level and choc and SignalSmith Stretch in the libs
directory. The libs will be made proper git submodules later.
