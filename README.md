# AudioPluginHost Mark II

Fork of the JUCE AudioPluginHost, with some improvements.

At the moment, the only difference is the added internal plugin for playing 
audio files, which is a work in progress.

Future ideas that may or may not ever be implemented :

- CPU meter, possibly for individual plugins too
- Input/Output level meters
- Implement tempo and audio playhead so plugins that depend on that can be used
- Record live output into a file
- Offline render
- Some super simple way to generate MIDI for testing instrument plugins
- Plugin parameter automation/modulation
- Use Tracktion Graph instead of the Juce AudioProcessorGraph
