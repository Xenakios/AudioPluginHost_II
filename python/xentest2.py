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

def test_plugin_scan():
    plugs = xenakios.ClapEngine.scanPluginDirs()
    for path in plugs:
        if "BYOD" in str(path):
            print(xenakios.ClapEngine.scanPluginFile(path))
        
    # 

# test_plugin_scan()  
def test_circle_stuff():
    p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clap',0)
    seq = xenakios.ClapSequence()
    t = 0.0
    freqs = [1.11,2.0,3.0,4.0]
    amps = [1.2,0.0,0.0,0.5]
    
    odur = 30
    lastx = 0.0
    lasty = 0.0
    while (t<odur):
        x = 0.0
        y = 0.0
        for i in range(len(freqs)):
            x = x + amps[i] * math.cos(2 * math.pi * t * freqs[i])
            y = y + amps[i] * math.sin(2 * math.pi * t * freqs[i])
        distance = (math.dist([0.0,0.0],[x,y]))
        semi = round(24.0+30.0*distance)
        if semi<0:
            semi = 0
        if semi>127:
            semi = 127
        seq.addNote(time=t,dur=0.2,key=semi)
        # print(f'{t} {semi}')
        distance = 0.05+0.2*math.dist([lastx,lasty],[x,y])
        if distance<0.05:
            distance = 0.05
        t = t + distance
        lastx = x
        lasty = y
    p.setSequence(seq)
    p.processToFile("surgerender.wav",odur+1.0,44100.0)

# print()

# test_circle_stuff()

def rwalk(curval,minval,maxval):
    step = -0.01+0.02*random.random()
    newval = curval + step
    if newval<minval:
        newval = minval
    if newval>maxval:
        newval = maxval
    return newval

def procession_intro():
    p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\NoisePlethoraSynth.clap',0)
    seq = xenakios.ClapSequence()
    pulselen = 2.0/32
    totaldur = 120.0
    
    primes = [7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97, 101,103,107,109,113,127,131]
    noteid = 0
    xpars = [0.1,0.1,0.1,0.1,0.2,0.2,0.2,0.2,0.2,0.6,0.6,0.6,0.6,0.9,0.9,0.9,0.9]
    ypars = [0.9,0.8,0.7,0.6,0.5,0.4,0.3,0.2,0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9]
    algos = [0,1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7]
    # envelope
    seq.addParameterEvent(time=0,parid=8,val=0.001)
    seq.addParameterEvent(time=0,parid=9,val=0.5)
    seq.addParameterEvent(time=0,parid=10,val=0.0)
    seq.addParameterEvent(time=0,parid=11,val=0.2)
    
    for i in range(16):
        # notelen = pulselen * primes[16-i]
        notelen = pulselen * (32-i)
        t = totaldur
        layerstart = totaldur*0.75/16*i
        while t >= 0.0:
            if t>=layerstart:
                randt = random.random()*0.02
                tpos = t+randt
                seq.addNote(time=tpos,dur=notelen*0.5,key=24+i*5,nid=noteid)
                seq.addParameterEvent(time=tpos,nid=noteid,parid=1,val=rwalk(xpars[i],0.0,1.0))
                seq.addParameterEvent(time=tpos,nid=noteid,parid=2,val=rwalk(ypars[i],0.0,1.0))
                # pan
                pan = -1.0+2.0/16*i
                seq.addParameterEvent(time=tpos,nid=noteid, parid=7,val=pan)
                # algo
                seq.addParameterEvent(time=tpos,parid=6,val=algos[i])
                noteid = noteid + 1
                # print(t)
            t = t - notelen
        # print(notelen)
    p.setSequence(seq)
    
    p.processToFile("procession_intro4.wav",totaldur+5.0,44100.0)

# procession_intro()

xep = xenakios.EnvelopePoint

def test_env_points():
    env = xenakios.Envelope()
    # env.removePoint(90)
    env.addPoint(xep(0.0,1.0))
    env.addPoint(xep(1.0,0.0))
    env.addPoint(xep(10.1,5.45))
    # print(env.getValueAtPosition(0.0))
    # print(env.getValueAtPosition(0.5))
    # print(env.getValueAtPosition(1.0))
    # print(env.getValueAtPosition(10.0))
    env.setPoint(2,xep(10.2,-45.111))
    # print(env.getValueAtPosition(9.0))
    # print(env.getValueAtPosition(10.0))
    for pt in env:
        print(pt)

test_env_points()

# 😊

def test_choc_window():
    
    p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clap',0)
    # time.sleep(1)
    # p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clappo',0)
    # p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\u-he\Zebralette3.clap',0)
    # try:
    #     p.loadStateFromFile("😊 zebralettestate 🦓.json")
    # except:
    #     pass
    # p.showGUIBlocking()
    seq = xenakios.ClapSequence()
    seq.addProgramChange(0.0,0,1,2)
    seq.addNote(time=2.0,dur=3.0,key=60)
    seq.addProgramChange(6.0,0,1,3)
    
    seq.addNote(time=8.0,dur=3.0,key=67)
    p.setSequence(seq)
    p.processToFile("surgerender.wav",15.0,44100.0)
    # p.saveStateToFile("😊 zebralettestate 🦓.json")
    # numpars = p.getNumParameters()
    # for i in range(numpars):
    #    print(p.getParameterInfoString(i))
    

# test_choc_window()

# print(f"yeah{f"{math.factorial(20)}"}")