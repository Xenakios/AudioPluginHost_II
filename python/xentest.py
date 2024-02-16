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

# test_clap()

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
    outdur = 10.0
    p = xenakios.NoisePlethoraEngine()
    seq = xenakios.ClapSequence()
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 0, 0.5)
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 1, 0.9)
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 3, -12.0)
    seq.addParameterEvent(False, 0.0, -1, -1, -1, -1, 5, 1)
    mm = xenakios.MultiModulator(44100.0)
    mm.setLFOProps(0, 0.0, 0.0, 3)
    mm.setLFOProps(1, 1.0, 0.0, 4)
    mm.setConnection(0, 0, 0, 1.0)
    mm.setConnection(1, 1, 1, 1.0)
    # mm.setConnection(2, 0, 1, 1.0)
    # mm.setConnection(1, 1, 0, 0.1)
    mm.setOutputAsParameterModulation(0, 3, -11.0)
    mm.setOutputAsParameterModulation(1, 0, -0.5)
    mm.applyToSequence(seq, 0.0, outdur)
    p.setSequence(seq)
    p.processToFile(f"npclap02.wav", 10.0)

test_plethora3()
