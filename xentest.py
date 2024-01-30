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
    # pars = p.getParameters()
    # print(pars)
    # return
    seq = xenakios.ClapSequence()
    for j in range(0,16):
        e0 = xenakios.Envelope()
        x = 0.0
        while x<61.0:
            e0.addPoint(xenakios.EnvelopePoint(x,-24.0+48.0*random.random()))
            x = x + 1.0
        notestart = j * 1.0
        notedur = 40.0
        seq.addNoteOn(notestart,0,0,60,1.0,j)
        # generate_expression_curve(60.0,0.02,seq,xenakios.constants.CLAP_NOTE_EXPRESSION_TUNING,0,0,60,j,
        #                        lambda a : e0.getValueAtPosition(a,0))
        xenakios.generateNoteExpressionsFromEnvelope(
            seq,e0,notestart,notedur,0.05,xenakios.constants.CLAP_NOTE_EXPRESSION_TUNING,0,0,60,j)
        seq.addNoteExpression(notestart,0,0,60,j,xenakios.constants.CLAP_NOTE_EXPRESSION_PAN,random.random())
        # seq.addNoteExpression(5.23,0,0,60,-1,xenakios.constants.CLAP_NOTE_EXPRESSION_PAN,0.5)
        seq.addNoteOff(notestart+notedur,0,0,60,1.0,j)
    # seq.addNoteOff(7.0,0,0,67,1.0,-1)
    p.setSequence(seq)
    p.processToFile("clap_out02.wav",61.0,44100)

test_clap()

# print(xenakios.constants.CLAP_NOTE_EXPRESSION_PAN)

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
