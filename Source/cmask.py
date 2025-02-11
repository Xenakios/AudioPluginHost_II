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

GM_RandomUniform = 0
GM_RandomExp = 1
GM_RandomGauss = 2
GM_RandomCauchy = 3
GM_RandomBeta = 4
GM_RandomArcSine = 5
GM_LFO_Sine = 100
GM_Constant = 1000
GM_ItemList = 1001


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
        self.items = items

    def get_par_value(self, source: float | xenakios.Envelope, t):
        if type(source) is xenakios.Envelope:
            return source.get_value(t)
        return source

    def generate_value_from_items(self):
        val = self.items[self.item_counter]
        self.item_counter += 1
        if self.item_counter == len(self.items):
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


def add_points(env: xenakios.Envelope, pts):
    for pt in pts:
        env.add_point(xenakios.EnvelopePoint(pt[0], pt[1]))


def make_envelope(pts: list) -> xenakios.Envelope:
    env = xenakios.Envelope()
    for pt in pts:
        env.add_point(xenakios.EnvelopePoint(pt[0], pt[1]))
    return env


def generate(parameters: list[Parameter], texturedur: float):
    events = []
    t = 0.0

    dt_threshold = 0.001
    dt_sanity_limit = 100
    dt_sanity_counter = 0
    while t < texturedur:
        event = []
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


def play_sequence(data):
    client = udp_client.SimpleUDPClient("127.0.0.1", 7001)
    events = []
    for ev in data:
        events.append((ev["t"], ev["pitch"], ev["velo"]))
        events.append((ev["t"] + ev["dur"], ev["pitch"], 0))
    events.sort(key=lambda x: x[0])
    time_start = time.time()
    cnt = 0
    timing_res = 0.010
    while cnt < len(events):
        time_cur = time.time() - time_start
        ev = events[cnt]
        while time_cur >= ev[0]:
            # print(f"{time_cur} {ev}")
            client.send_message("/mnote", [float(ev[1]), float(ev[2] * 127.0)])
            cnt += 1
            if cnt == len(events):
                break
            ev = events[cnt]
        time.sleep(timing_res)
    print("done")


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
    colors = ["cyan", "red", "green", "yellow", "pink", "white", "magenta"]
    ax1.set_xlabel("time (s)")
    ax1.scatter(xarr, yarrs[0], color=colors[0])
    for i in range(1, len(yarrs)):
        ax2 = ax1.twinx()
        ax2.scatter(xarr, yarrs[i], color=colors[i])
    plt.get_current_fig_manager().window.state("zoomed")
    fig.tight_layout()
    plt.show()


def test_generate():
    params: list[Parameter] = []
    params.append(
        Parameter(0, 0.0, 1.0, genmethod=GM_RandomExp, genpar0=4.0, rseed=43)
    )
    params.append(
        Parameter(
            1,
            48.0,
            72.0,
            mask_lower=make_envelope([(0.0, 48.0), (5.0, 48.0), (10.0, 48.0)]),
            mask_higher=make_envelope([(0.0, 72.0), (5.0, 72.0), (10.0, 72.0)]),
            genmethod=GM_RandomBeta,
            genpar0=make_envelope([(0.0, 0.05), (5.0, 0.05), (8.0, 10.0)]),
            genpar1=make_envelope([(0.0, 0.05), (5.0, 0.05), (8.0, 10.0)]),
        )
    )
    params[-1] = Parameter(
        1, 48.0, 72.0, genmethod=GM_ItemList, items=[60.0, 63.0, 67.0, 72.0]
    )
    dur = 10.0
    events = generate(params, dur)
    density = len(events) / dur
    print(f"{len(events)} events, density ~{density} events/s")
    plot_events(params, events)


test_generate()
# print(random.betavariate())

# print(len(evts))
# play_sequence(evts)
# s0 = SV(3, 1)
# print(s0.get_list(0, 10))
