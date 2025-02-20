import xenakios
import xepylib
import math
import time
import random
from matplotlib import pyplot as plt
import xepylib.sieves
import xepylib.xenutils
from pythonosc import udp_client
from xepylib.sieves import Sieve as SV
from xenakios import Envelope as Envelope

# Reimplementation/reimagining of Cmask by Andre Bartetzki
# https://abartetzki.users.ak.tu-berlin.de/CMaskMan/CMask-Manual.htm
#
# The original Cmask was developed to produce Csound scores which are basically
# just lists of lists of numbers. We are following that model for now here but
# recent developments like MIDI MPE and the Clap plugin format would allow for a richer
# system where the scores contain more complicated objects.

GM_RandomUniform = 0
GM_RandomExp = 1
GM_RandomGauss = 2
GM_RandomCauchy = 3
GM_RandomBeta = 4
GM_RandomArcSine = 5
GM_LFO_Sine = 100
GM_Constant = 1000
GM_ItemList = 1001

IM_Forward = 0
IM_ForwardBackward = 1
IM_Shuffle = 2
IM_Random = 3


class Parameter:
    def __init__(
        self,
        target_id,
        minval: float,
        maxval: float,
        genmethod: int = GM_Constant,
        genpar0: float | None = None,
        genpar1: float | None = None,
        mask_lower=None,
        mask_higher=None,
        items: list[float] | None = None,
        itemsmode: int = None,
        color: str = "white",
        rseed=None,
    ):
        self.rng = random.Random(rseed)
        self.target_id = target_id
        self.minval = minval
        self.maxval = maxval
        if mask_lower is not None:
            self.env_low = mask_lower
        else:
            self.env_low = minval
        if mask_higher is not None:
            self.env_high = mask_higher
        else:
            self.env_high = maxval

        self.shaping = 0.0
        self.quan_interval = None
        self.quanstrength = 1.0
        self.quanshift = 0.0
        self.valuelist = None
        self.gen_par0 = genpar0
        self.gen_par1 = genpar1
        self.color = color
        self.last_t = 0.0
        self.osc_phase = 0.0
        self.genmethod = genmethod

        self.item_counter = 0
        self.item_counter_inc = 1
        self.itemsmode = itemsmode
        self.items = items

    def get_par_value(self, source: float | xenakios.Envelope, t):
        if type(source) is xenakios.Envelope:
            return source.get_value(t)
        return source

    def generate_value_from_items(self):
        if self.itemsmode == IM_Random:
            return self.rng.choice(self.items)
        val = self.items[self.item_counter]
        self.item_counter += self.item_counter_inc
        if self.item_counter == len(self.items):
            if self.itemsmode == IM_Forward:
                self.item_counter = 0
            if self.itemsmode == IM_ForwardBackward:
                self.item_counter_inc = -1
                self.item_counter = len(self.items) - 1
            if self.itemsmode == IM_Shuffle:
                self.item_counter = 0
                self.rng.shuffle(self.items)
        if self.itemsmode == IM_ForwardBackward and self.item_counter < 0:
            self.item_counter_inc = 1
            self.item_counter = 0
        return val

    def generate_value(self, t: float):
        if self.genmethod == GM_ItemList:
            return self.generate_value_from_items()

        z = 0.5
        genpar0 = self.get_par_value(self.gen_par0, t)
        genpar1 = self.get_par_value(self.gen_par1, t)
        if self.genmethod == GM_RandomUniform:
            z = self.rng.random()
        elif self.genmethod == GM_RandomGauss:
            z = self.rng.gauss(genpar0, genpar1)
        elif self.genmethod == GM_RandomExp:
            z = self.rng.expovariate(genpar0)
        elif self.genmethod == GM_RandomCauchy:
            z = xepylib.xenutils.random_cauchy(genpar0, genpar1, self.rng.random())
        elif self.genmethod == GM_RandomArcSine:
            z = 0.5 - 0.5 * math.sin((0.5 - self.rng.random()) * math.pi)
        elif self.genmethod == GM_RandomBeta:
            z = self.rng.betavariate(genpar0, genpar1)
        elif self.genmethod == GM_LFO_Sine:
            z = 0.5 + 0.5 * math.sin(2 * math.pi * self.osc_phase)
        elif self.genmethod == GM_Constant:
            z = genpar0
        z = xepylib.xenutils.clamp(z, 0.0, 1.0)
        if self.shaping < 0.0:
            shaping_exp = xepylib.xenutils.map_value(self.shaping, -1.0, 0.0, 4.0, 1.0)
            z = 1.0 - (1.0 - z) ** shaping_exp
        else:
            shaping_exp = xepylib.xenutils.map_value(self.shaping, 0.0, 1.0, 1.0, 4.0)
            z **= shaping_exp
        limlow = self.get_par_value(self.env_low, t)
        limhigh = self.get_par_value(self.env_high, t)
        val = xepylib.xenutils.map_value(z, 0.0, 1.0, limlow, limhigh)
        if self.quan_interval is not None:
            qinterval = 1.0 / self.quan_interval
            tempval = math.floor(qinterval * val) / qinterval
            qshift = self.get_par_value(self.quanshift, t)
            tempval = xepylib.xenutils.wrap(tempval + qshift, self.minval, self.maxval)
            qs = self.get_par_value(self.quanstrength, t)
            qs = xepylib.xenutils.clamp(qs, 0.0, 1.0)
            val = val * (1.0 - qs) + tempval * qs
        tdiff = t - self.last_t
        if genpar0 is not None:
            self.osc_phase += tdiff * 2.0**genpar0
        self.last_t = t
        return val


def make_envelope(pts: list) -> xenakios.Envelope:
    env = xenakios.Envelope()
    for pt in pts:
        env.add_point(xenakios.EnvelopePoint(pt[0], pt[1]))
    return env


def generate(parameters: list[Parameter], texturedur: float):
    events: list[list[float]] = []
    t = 0.0

    dt_threshold = 0.001
    dt_sanity_limit = 100
    dt_sanity_counter = 0
    while t < texturedur:
        event: list[float] = []
        dt = parameters[0].generate_value(t)
        if dt < dt_threshold:
            dt_sanity_counter += 1
            if dt_sanity_counter == dt_sanity_limit:
                raise RuntimeError(
                    "time delta was too small too many times, generation would take a very long or infinite time"
                )
        else:
            dt_sanity_counter = 0
        event.append(t)
        for i in range(1, len(parameters)):
            val = parameters[i].generate_value(t)
            event.append(val)
        events.append(event)
        t += dt

    return events


OSCEvent_NoteOn = 0
OSCEvent_NoteOff = 1
OSCEvent_Expression = 2
OSCEvent_Param = 3


def play_osc_sequence(events):
    client = udp_client.SimpleUDPClient("127.0.0.1", 7001)
    time_start = time.time()
    cnt = 0
    timing_res = 0.01
    error_sum = 0.0
    while cnt < len(events):
        time_cur = time.time() - time_start
        ev = events[cnt]
        while time_cur >= ev[0]:
            error_sum += time_cur - ev[0]
            # print(f"{time_cur} {ev}")
            if ev[1] == OSCEvent_NoteOn:
                client.send_message("/mnote", [float(ev[2]), float(ev[3] * 1.0)])
            if ev[1] == OSCEvent_Expression:
                client.send_message("/nexp", [ev[2], ev[3], ev[4]])
            if ev[1] == OSCEvent_Param:
                client.send_message(ev[2], ev[3])
            cnt += 1
            if cnt == len(events):
                break
            ev = events[cnt]
        time.sleep(timing_res)
    avg_error = error_sum / (cnt - 1)
    print(f"done, {avg_error} avg error in timing")


def plot_events(parameters: list[Parameter], events: list[list]):
    xarr = []
    yarrs = []
    for _ in range(0, len(parameters) - 1):
        yarrs.append([])
    for ev in events:
        xarr.append(ev[0])
        for i in range(0, len(parameters) - 1):
            yarrs[i].append(ev[i + 1])

    plt.style.use("dark_background")
    fig, ax1 = plt.subplots()
    ax1.set_xlabel("time (s)")
    ax1.scatter(xarr, yarrs[0], color=parameters[1].color)
    for i in range(1, len(yarrs)):
        ax2 = ax1.twinx()
        ax2.scatter(xarr, yarrs[i], color=parameters[i + 1].color)
    plt.get_current_fig_manager().window.state("zoomed")
    fig.tight_layout()
    plt.show()


class PluginParameterAutomation:
    def __init__(self, param_addr: str, env: Envelope):
        self.addr = param_addr
        self.env = env
        pass


def generate_osc_sequence(
    parameters: list[Parameter],
    maskevents: list[list],
    paraut: PluginParameterAutomation,
):
    osc_events = []
    note_id = 0
    for ev in maskevents:
        oev_on = [ev[0], OSCEvent_NoteOn, ev[2], ev[3], float(note_id)]
        osc_events.append(oev_on)
        oev_pan = [ev[0], OSCEvent_Expression, note_id, 1, ev[4]]
        osc_events.append(oev_pan)
        oev_off = [ev[0] + ev[1], OSCEvent_NoteOn, ev[2], 0.0, float(note_id)]
        osc_events.append(oev_off)
        note_id += 1
    t = 0.0
    dur = paraut.env.get_point(paraut.env.num_points() - 1).x()
    resol = 0.1
    while t < dur:
        val = paraut.env.get_value(t)
        osc_events.append([t, OSCEvent_Param, paraut.addr, val])
        t += resol
    osc_events.sort(key=lambda x: x[0])
    return osc_events


def test_generate():
    maskparams: list[Parameter] = []
    maskparams.append(
        Parameter(
            "timepos",
            0.0,
            1.0,
            genmethod=GM_RandomExp,
            genpar0=4.0,
            rseed=43,
            color="#FF000000",
        )
    )
    # params[-1] = Parameter(0, 0.0, 1.0, genmethod=GM_Constant, genpar0=0.1)

    maskparams.append(
        Parameter(
            "duration", 0.0, 1.0, genmethod=GM_Constant, genpar0=0.1, color="#FF000000"
        )
    )
    maskparams.append(
        Parameter(
            "pitch",
            48.0,
            72.0,
            mask_lower=Envelope(
                [(0.0, 72.0, 0.8), (5.0, 48.0, -0.8), (10.0, 72.0, 0.0)]
            ),
            mask_higher=Envelope([(0.0, 72.0), (5.0, 72.0), (10.0, 72.0)]),
            genmethod=GM_RandomUniform,
            genpar0=0.1,
            genpar1=0.1,
            color="yellow",
        )
    )
    maskparams.append(
        Parameter(
            "velocity",
            0.0,
            127.0,
            genmethod=GM_ItemList,
            items=[127.0, 63.0, 63.0, 63.0, 10.0, 10.0, 10.0],
            color="#FF000000",
        )
    )
    maskparams.append(
        Parameter(
            "pan", 0.0, 1.0, genmethod=GM_LFO_Sine, genpar0=-1.0, color="#FF000000"
        )
    )

    dur = 10.0
    events = generate(maskparams, dur)
    density = len(events) / dur
    print(f"{len(events)} events, density ~{density} events/s")
    # plot_events(maskparams, events)
    paraut = PluginParameterAutomation(
        "/param/main_pan", Envelope([(0.0, 0.0), (10.0, 1.0)])
    )
    osc_sequence = generate_osc_sequence(maskparams, events, paraut)
    play_osc_sequence(osc_sequence)


# test_generate()


def test_itemlist_modes():
    dur = 10.0
    params: list[Parameter] = []
    params.append(
        Parameter(
            "timepos",
            0.0,
            1.0,
            genmethod=GM_RandomExp,
            genpar0=4.0,
            rseed=43,
            color="#FF000000",
        )
    )
    params.append(
        Parameter(
            "pan",
            0.0,
            1.0,
            genmethod=GM_ItemList,
            items=[0.0, 0.25, 0.5, 0.75, 1.0],
            itemsmode=IM_Random,
            color="yellow",
        )
    )
    t0 = time.time()
    events = generate(params, dur)
    t1 = time.time()
    print(f"elapsed time {t1 - t0:.2f} seconds, {len(events)} events generated")
    plot_events(params, events)


test_itemlist_modes()
