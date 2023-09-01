# AudioPluginHost Mark II

Fork of the JUCE AudioPluginHost, with some improvements.

At the moment, the only difference is the added internal plugin for playing 
audio files, which is a work in progress.

Future ideas :
- CPU meter, possibly for individual plugins too
- Output level meter
- Implement tempo and audio playhead so plugins that depend on that can be used
- Record live output into a file
- Offline render
- Some super simple way to generate MIDI for testing instrument plugins
