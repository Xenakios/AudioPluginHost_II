import xenakios
import random
import math

def generate_expression_curve(dur, granularity, seq, net, port, chan, key, noteid, func):
    numevents = dur / granularity
    for i in range(0,int(numevents)):
        evtime = i*granularity
        seq.addNoteExpression(evtime,port,chan,key,noteid,net,func(evtime))

def test_clap():
    p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clap',0)
    p.showGUIBlocking()
    
    # pars = p.getParameters()
    # print(pars)
    return
    # 3484083994 global volume
    seq = xenakios.ClapSequence()
    mastvolenv = xenakios.Envelope()
    mastvolenv.addPoint(xenakios.EnvelopePoint(0.0,0.0))
    mastvolenv.addPoint(xenakios.EnvelopePoint(5.0,0.5))
    mastvolenv.addPoint(xenakios.EnvelopePoint(10.0,0.5))
    mastvolenv.addPoint(xenakios.EnvelopePoint(11.0,0.0))
    mastvolenv.addPoint(xenakios.EnvelopePoint(13.0,0.0))
    mastvolenv.addPoint(xenakios.EnvelopePoint(14.0,0.6))
    mastvolenv.addPoint(xenakios.EnvelopePoint(55.0,0.5))
    mastvolenv.addPoint(xenakios.EnvelopePoint(61.0,0.0))
    xenakios.generateParameterEventsFromEnvelope(False,seq,mastvolenv,0.0,61.0,0.05,3484083994,-1,-1,-1,-1)
    for j in range(0,16):
        e0 = xenakios.Envelope()
        x = 0.0
        while x<61.0:
            e0.addPoint(xenakios.EnvelopePoint(x,-24.0+48.0*random.random()))
            x = x + 5.0
        x = 0.0
        e1 = xenakios.Envelope()
        while x<61.0:
            e1.addPoint(xenakios.EnvelopePoint(x,0.0+4.0*random.random()))
            x = x + 1.0
        
        notestart = 0
        notedur = 60.0
        seq.addNoteOn(notestart,0,0,60,1.0,j)
        xenakios.generateNoteExpressionsFromEnvelope(
            seq,e0,notestart,notedur,0.05,xenakios.constants.CLAP_NOTE_EXPRESSION_TUNING,0,0,60,j)
        xenakios.generateNoteExpressionsFromEnvelope(
            seq,e1,notestart,notedur,0.05,xenakios.constants.CLAP_NOTE_EXPRESSION_VOLUME,0,0,60,j)
        seq.addNoteExpression(notestart,0,0,60,j,xenakios.constants.CLAP_NOTE_EXPRESSION_PAN,random.random())
        # seq.addNoteExpression(5.23,0,0,60,-1,xenakios.constants.CLAP_NOTE_EXPRESSION_PAN,0.5)
        seq.addNoteOff(notestart+notedur,0,0,60,1.0,j)
    # seq.addNoteOff(7.0,0,0,67,1.0,-1)
    p.setSequence(seq)
    print(f"sequence has {seq.getNumEvents()} events")
    p.processToFile("clap_out02.wav",61.0,44100)

def test_clap2():
    p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clap',0)
    seq = xenakios.ClapSequence()
    pulselen = 0.15
    outdur = 20.0
    t = 0.0
    while t<outdur:
        key = random.randint(43,60)
        velo = 0.5
        if random.random()<0.5:
            velo = 0.99
        seq.addNote(t,0.5,0,0,key,-1,velo)
        
        t = t + pulselen * random.randint(1,5)
    t = 0.0
    while t<outdur:
        key = random.randint(60,76)
        velo = 0.5
        if random.random()<0.5:
            velo = 0.99
        seq.addNote(t,0.5,0,0,key,-1,velo)
        
        t = t + pulselen * random.randint(1,2)
    p.setSequence(seq)
    p.processToFile("clap_out03.wav",outdur+2.0,44100)

# test_clap2()

"""
Volume (ID 0) range [-24.000000 - 0.000000] Automatable/Modulatable/PerNoteID
Pan (ID 7) range [-1.000000 - 1.000000] Automatable/Modulatable/PerNoteID
X (ID 1) range [0.000000 - 1.000000] Automatable/Modulatable/PerNoteID
Y (ID 2) range [0.000000 - 1.000000] Automatable/Modulatable/PerNoteID
Algorithm (ID 6) range [0.000000 - 29.000000] Automatable/Modulatable/PerNoteID
Filter cut off (ID 3) range [0.000000 - 127.000000] Automatable/Modulatable/PerNoteID
Filter resonance (ID 4) range [0.010000 - 0.990000] Automatable/Modulatable/PerNoteID
Filter type (ID 5) range [0.000000 - 1.000000] Automatable/Modulatable/PerNoteID
"""

def test_clap3():
    
    p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\NoisePlethoraSynth.clap',0)
    # p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\Conduit.clap',1)
    # p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\u-he\Zebralette3.clap',0)
    # p = xenakios.ClapEngine(r'C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clap',0)
    # p.loadStateFromFile("surgestate.bin")
    # return
    numpars = p.getNumParameters()
    for i in range(numpars):
        parinfo = p.getParameterInfoString(i)
        # print(parinfo)
    # p.showGUIBlocking()
    seq = xenakios.ClapSequence()
    
    # seq.addNote(5.0,5.0,0,0,61,-1,0.9)
    
    # seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 3, 84.0)
    # seq.addParameterEvent(False, 0.0, -1,-1,-1,-1,5,3)
    # seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 4, 0.90 )
    
    # attack
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 8, 0.01 )
    # decay
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 9, 0.1 )
    # sustain
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 10, 0.5 )
    # release
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 11, 0.4 )
    
    t = 0.0
    i = 0
    while t<10.5:
        # if random.random()<0.5:
        seq.addNote(t,0.1,0,0,60,i,0.9)
        # seq.addParameterEvent(False,t, -1, -1, -1, -1, 3, 84.0+36.5*math.sin(2*3.141592653*t*0.25))
        # seq.addParameterEvent(False,t, -1, -1, -1, -1, 4, 0.5+0.5*math.sin(2*3.141592653*t*0.5))
        t = t + 0.05
        i = i + 1
    p.setSequence(seq)
    p.processToFile("clap_noiseplethora_out02.wav",15.0,44100)
    return
    seq.addParameterEvent(False, 0.0, 0, 0, -1, -1, 3, 84.0)
    seq.addParameterEvent(False, 0.0, 0, 0, -1, -1, 4, 0.4)
    seq.addParameterEvent(False, 7.5, 0, 0, -1, -1, 4, 0.9)
    t = 0.0
    while t < 10.0:
        seq.addParameterEvent(False, t, 0, 0, -1, -1, 5, random.randint(0,2))
        seq.addParameterEvent(True, t, 0, 0, -1, -1, 3, 12.0*random.random())
        # seq.addParameterEvent(True, t, 0, 0, -1, -1, 3, 12.0*random.random())
        t = t + 0.2
    
    
    if False:
        seq.addParameterEvent(False, 0.0, 0, 0, -1, -1, 2, 0.2)
        seq.addParameterEvent(False, 3.0, 0, 0, -1, -1, 2, 0.8)
        seq.addParameterEvent(False, 6.2, 0, 0, -1, -1, 2, 0.4)
        seq.addParameterEvent(False, 7.5, 0, 0, -1, -1, 2, 0.9)
        seq.addNote(0.0,2.0,0,0,60,-1,0.9)
        seq.addNote(3.0,2.0,0,0,60,0,0.9)
        seq.addNote(5.0,4.0,0,0,60,1,0.9)
        seq.addNote(6.0,4.0,0,0,60,2,0.9)
        seq.addParameterEvent(False, 3.0, 0, 0, 60, 0, 0, -12.0)
        seq.addParameterEvent(False, 4.0, 0, 0, 60, 0, 0, -3.0)
        seq.addParameterEvent(False, 4.5, 0, 0, -1, 0, 1, 0.1)
        seq.addParameterEvent(False, 5.0, 0, 0, -1, 1, 1, 0.2)
        seq.addParameterEvent(False, 5.5, 0, 0, -1, 1, 1, 0.3)
        seq.addParameterEvent(False, 6.0, 0, 0, -1, 1, 1, 0.6)
        t = 6.0
        a = 0
        while t<10.0:
            seq.addParameterEvent(False, t, 0, 0, -1, 1, 5, a)
            seq.addParameterEvent(False, t, 0, 0, -1, 1, 6, random.random())
            seq.addParameterEvent(False, t, 0, 0, -1, 2, 6, random.random())
            seq.addParameterEvent(False, t, 0, 0, -1, 2, 1, random.random())
            t = t + 0.125
            a = a + 1
        seq.addParameterEvent(False, 0.0, 0, 0, -1, -1, 4, 0.3)
        t = 0
        while t<10.0:
            seq.addParameterEvent(False, t, 0, 0, -1, -1, 3, 84+12.0* math.sin(2*3.141592*t*0.6))
            t = t + 0.1
    p.setSequence(seq)
    p.processToFile("clap_noiseplethora_out02.wav",10.0,44100)
    # p.saveStateToFile("surgestate.bin")

test_clap3()

def test_dejavu():
    dejavu = xenakios.DejaVuRandom(1)
    dejavu.setDejaVu(0.45)
    for i in range(10):
        print(dejavu.nextInt(0,3),end =" ")

# test_dejavu()

def test_plethora():
    xenakios.list_plugins()
    p = xenakios.NoisePlethoraEngine("radioOhNo")
    e0 = xenakios.Envelope()
    e0.addPoint(xenakios.EnvelopePoint(0.0,-0.5))
    e0.addPoint(xenakios.EnvelopePoint(5.0,0.5))
    e0.addPoint(xenakios.EnvelopePoint(10.0,-0.5))
    p.setEnvelope(0,e0)
    e1 = xenakios.Envelope()
    e1.addPoint(xenakios.EnvelopePoint(0.0,-0.5))
    e1.addPoint(xenakios.EnvelopePoint(1.0,0.5))
    e1.addPoint(xenakios.EnvelopePoint(10.0,-0.5))
    p.setEnvelope(1,e1)
    e2 = xenakios.Envelope()
    e2.addPoint(xenakios.EnvelopePoint(0.0,0.0))
    e2.addPoint(xenakios.EnvelopePoint(5.0,120.0))
    e2.addPoint(xenakios.EnvelopePoint(9.0,120.0))
    e2.addPoint(xenakios.EnvelopePoint(10.0,0.0))
    p.setEnvelope(2,e2)
    p.highpass = 12
    p.processToFile(f"out043.wav",10.0,0.5,0.5)
    # for i in range(0,5):
    #    p.process_to_file(f"out{i}.wav",1.0,0.1,i*0.2)

def test_plethora2():
    
    p = xenakios.NoisePlethoraEngine()
    seq = xenakios.ClapSequence()
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 0, 0.5)
    seq.addParameterEvent(False, 1.0, -1, -1, -1, -1, 0, 0.0)
    seq.addParameterEvent(False, 2.0, -1, -1, -1, -1, 0, 0.9)
    seq.addParameterEvent(False, 7.6, -1, -1, -1, -1, 0, 0.3)
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 1, 0.5)
    seq.addParameterEvent(False, 5.0, -1, -1, -1, -1, 1, 0.2)

    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 3, -20.0)
    seq.addParameterEvent(False, 1.0, -1, -1, -1, -1, 3, -6.0)
    seq.addParameterEvent(False, 5.0, -1, -1, -1, -1, 3, -12.0)
    seq.addParameterEvent(False, 5.5, -1, -1, -1, -1, 3, -3.0)
    seq.addParameterEvent(False, 7.8, -1, -1, -1, -1, 3, -11.0)
    
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 5, 3)
    seq.addParameterEvent(False, 1.5, -1, -1, -1, -1, 5, 0)
    seq.addParameterEvent(False, 6.22, -1, -1, -1, -1, 5, 20)
    t = 10
    while t<30.0:
        seq.addParameterEvent(False, t, -1, -1, -1, -1, 5, random.randint(0,29))
        t = t + 0.5

    t = 0.0
    while t<10:
        # seq.addParameterEvent(False, t, -1, -1, -1, -1, 4, 0.70)
        # seq.addParameterEvent(False, t + 0.1, -1, -1, -1, -1, 4, 0.01)
        t = t + 0.3

    t = 0.0
    while t<10:
        # seq.addParameterEvent(False, t, -1, -1, -1, -1, 2, 90.0+10.0* math.sin(t*4))
        t = t + 0.01
    
    p.setSequence(seq)
    p.highpass = 12
    p.processToFile(f"npclap01.wav",30.0)

# test_plethora2()
    
def test_plethora3():
    outdur = 20.0
    p = xenakios.NoisePlethoraEngine()
    seq = xenakios.ClapSequence()
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 0, 0.2)
    seq.addParameterEvent(False, 5.0, -1, -1, -1, -1, 0, 0.1)
    seq.addParameterEvent(False, 10.0, -1, -1, -1, -1, 0, 0.33)
    seq.addParameterEvent(False, 12.0, -1, -1, -1, -1, 0, 0.99)
    seq.addParameterEvent(False, 13.0, -1, -1, -1, -1, 0, 0.01)
    seq.addParameterEvent(False, 14.2, -1, -1, -1, -1, 0, 0.22)
    seq.addParameterEvent(False, 15.2, -1, -1, -1, -1, 0, 0.45)
    seq.addParameterEvent(False, 15.2, -1, -1, -1, -1, 1, 0.45)
    
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 1, 0.6)
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 3, -12.0)
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 5, 2)
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 2, 84.0)
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 4, 0.5)
    seq.addParameterEvent(False, 5.4, -1, -1, -1, -1, 5, 3)
    seq.addParameterEvent(False, 6.6, -1, -1, -1, -1, 5, 2)
    seq.addParameterEvent(False, 14.0, -1, -1, -1, -1, 5, 4)
    seq.addParameterEvent(False, 14.5, -1, -1, -1, -1, 5, 5)
    seq.addParameterEvent(False, 15.0, -1, -1, -1, -1, 5, 6)
    seq.addParameterEvent(False, 15.5, -1, -1, -1, -1, 5, 7)

    mm = xenakios.MultiModulator(44100.0)
    
    mm.setLFOProps(0, 0.0, 0.0, 3)
    mm.setLFOProps(1, 1.1, 0.0, 4)
    mm.setLFOProps(2, 3.5, 0.0, 5)
    mm.setLFOProps(3, 2.0, -0.8, 4)

    mm.setConnection(0, 0, 0, 1.0)
    mm.setConnection(1, 1, 1, 1.0)
    mm.setConnection(2, 2, 2, 1.0)
    mm.setConnection(3, 3, 3, 1.0)
    # mm.setConnection(2, 0, 1, 1.0)
    # mm.setConnection(1, 1, 0, 0.1)
    mm.setOutputAsParameterModulation(0, 3, -11.0)
    # mm.setOutputAsParameterModulation(1, 0, 0.4)
    mm.setOutputAsParameterModulation(2, 2, 18.0)
    mm.setOutputAsParameterModulation(3, 4, 0.4)
    
    mm.applyToSequence(seq, 0.0, outdur)
    p.setSequence(seq)
    print(f'sequence size is {seq.getSizeInBytes()/1024.0/1024.0} megabytes')
    print(f'sequence has {seq.getNumEvents()} events')
    p.processToFile(f"npclap02.wav", outdur)

# test_plethora3()
