# Soon To Be Renamed Project

An environment for hosting Clap plugins.

How this will exactly work is in the works, but the main idea is to provide
facilities for working in the Python programming language, both for offline
and realtime processing. 

```
import xenakios
eng = xenakios.ClapEngine()
eng.addPlugin(r"Surge Synth Team\Shortcircuit XT.clap", 0)
seq = eng.getSequence(0)
seq.addNote(time=0.0, dur=2.0, key=60, velo=0.7)
seq.addNote(time=1.0, dur=2.0, key=67, velo=0.7)
eng.processToFile(
        r"out.wav",
        6.0,
        44100.0,
        2
    )
```

