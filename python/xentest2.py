import xenakios
import random
import math
import time

def test_fileplayer_clap():
    p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\FilePlayerPlugin.clap',0)
    seq = xenakios.ClapSequence()
    seq.addString(0, r'C:\MusicAudio\sourcesamples\count.wav')
    seq.addString(1, r'C:\MusicAudio\sourcesamples\db_guit01.wav')
    seq.addString(2, r'C:\MusicAudio\sourcesamples\there was a time .wav')
    seq.addString(3, r'C:\MusicAudio\sourcesamples\wakwakwak.wav')
    
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 9999, 0.01)
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 8888, 0.05)
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 1001, 0.0)
    seq.addParameterEvent(False, 1.0, -1, -1, -1, -1, 1001, 1.0)
    t = 2.0
    while t<7.0:
        rate = 0.0-(1.0/5.0)*(t-2.0)
        seq.addParameterEvent(False, t, -1, -1, -1, -1, 1001, rate)
        t = t + 0.1
    t = 0.0
    while t<20.0:
        seq.addStringEvent(t,-1,-1,-1,-1,random.randint(0,3),0)    
        t = t + 0.5
    t = 10.0
    while t<20.0:
        rate = 0.0+(3.0/9.0)*(t-10.0)
        seq.addParameterEvent(False, t, -1, -1, -1, -1, 1001, rate)
        t = t + 0.1
    seq.addParameterEvent(False, 5.0, -1, -1, -1, -1, 9999, 0.12)
    seq.addParameterEvent(False, 5.0, -1, -1, -1, -1, 8888, 0.24)
    p.setSequence(seq)
    p.processToFile("file_player_out_01.wav",20.0,44100)

# test_fileplayer_clap()

def test_fileplayer_clap2():
    p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\FilePlayerPlugin.clap',0)
    seq = xenakios.ClapSequence()
    
    # play mode
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 2022, 1.0)
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 1001, 0.5)
    seq.addParameterEvent(False, 5.0, -1, -1, -1, -1, 1001, -0.5)
    seq.addParameterEvent(False, 6.5, -1, -1, -1, -1, 1001, 0.0)
    seq.addParameterEvent(False, 7.0, -1, -1, -1, -1, 1001, 0.2)
    seq.addParameterEvent(False, 7.5, -1, -1, -1, -1, 1001, -3.0)
    seq.addParameterEvent(False, 9.5, -1, -1, -1, -1, 1001, 0.0)
    # seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 1001, -1.0)
    # seq.addParameterEvent(False, 5.0, -1, -1, -1, -1, 1001, 0.0)
    # seq.addParameterEvent(False, 10.0, -1, -1, -1, -1, 1001, 1.0)
    # seq.addParameterEvent(False, 13.0, -1, -1, -1, -1, 1001, -2.0)
    # seq.addParameterEvent(False, 3.0, -1, -1, -1, -1, 12001, 1.0)
    # seq.addParameterEvent(False, 4.0, -1, -1, -1, -1, 12001, 0.0)
    # seq.addParameterEvent(False, 5.0, -1, -1, -1, -1, 12001, 1.0)
    # seq.addParameterEvent(False, 6.0, -1, -1, -1, -1, 12001, 0.0)
    # seq.addParameterEvent(False, 9.0, -1, -1, -1, -1, 12001, 1.0)
    # seq.addParameterEvent(False, 13.0, -1, -1, -1, -1, 12001, 0.0)
    # seq.addParameterEvent(False, 15.5, -1, -1, -1, -1, 12001, 1.0)
    t = 0.0
    while t<20.0:
        # seq.addParameterEvent(False, t, -1, -1, -1, -1, 44, -3.0+6.0*random.random())
        t = t + 0.1
    
    p.setSequence(seq)
    p.processToFile("file_player_out_01.wav",10.0,44100)


# test_fileplayer_clap2()

def test_clap():
    p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clap',0)
    seq = xenakios.ClapSequence()
    seq.addNote(time=0.0,dur=0.9,key=60)
    seq.addNote(time=1.0,dur=0.9,key=60,retune=0.25)
    seq.addNote(time=2.0,dur=0.9,key=60,retune=0.0)
    seq.addNote(time=3.0,dur=0.9,key=60,retune=-0.25)

    seq.addNoteF(time=4.0,dur=0.9,pitch=60)
    seq.addNoteF(time=5.0,dur=0.9,pitch=60.25)
    seq.addNoteF(time=6.0,dur=0.9,pitch=60.0)
    seq.addNoteF(time=7.0,dur=0.9,pitch=59.75)

    p.setSequence(seq)
    p.processToFile("clap_out_b01.wav",10.0,44100)

# test_clap()

def ssscompens():
    sr = 44100.0
    playrate = 0.9
    playpos = 0.0
    outdur = 10.0
    outdursamples = outdur*sr
    blocksize = 512
    while playpos<outdursamples:
        t0 = round(playpos)
        t1 = round(playpos+playrate*blocksize)
        adjustedblocksize = (t1-t0)*playrate
        if playpos<sr:
            print(math.floor(adjustedblocksize))
        playpos = playpos + adjustedblocksize
    print(playpos/sr)

# ssscompens()

def test_choc_window():
    # p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clap',0)
    # p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clappo',0)
    p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\u-he\Zebralette3.clap',0)
    p.loadStateFromFile("☺ zebralettestate.json")
    p.showGUIBlocking()
    # p.processToFile("zebrarender.wav",1.0,44100.0)
    p.saveStateToFile("☺ zebralettestate.json")
    # numpars = p.getNumParameters()
    # for i in range(numpars):
    #    print(p.getParameterInfoString(i))
    

test_choc_window()

# print(f"yeah{f"{math.factorial(20)}"}")