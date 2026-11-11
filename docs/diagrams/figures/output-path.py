"""From a channel's registers to the pin: full OFF, full ON, the counter, sleep, INVRT, OUTDRV and OE.

Reads: Left to right, what decides the level of output LEDn (tables 6, 7 and 11). The channel logic is off when
LEDn_OFF_H bit 4 is set, whatever else the registers hold; otherwise on when LEDn_ON_H bit 4 is set; otherwise
asserted from the ON count to the OFF count of the 12-bit counter. Asleep, the oscillator is off and an output can
be neither switched on nor off. INVRT inverts the logic while OE is low; OUTDRV makes a high level a driven high
(totem pole) or high impedance (open drain). OE high replaces everything with the OUTNE level, which the driver sets
to the level of a channel that is off. The table at the bottom gives that level for each configuration, and whether
the reset state (not inverted, OUTNE = 00) is off for the load.
"""

import _source as S
from figkit import Fig


def data():
    hpp = S.read(S.HPP)
    S.need("return reg::kOutneLow;" in hpp and "reg::kOutneHigh : reg::kOutneHiZ" in hpp, "OffLevelOutne()")
    S.need("IsOff(value)" in S.functions()["applyOne"], "full OFF decides a switch-off")
    return {
        "outne": {"low": S.const("kOutneLow"), "high": S.const("kOutneHigh"), "hiz": S.const("kOutneHiZ")},
        "sources": [f"{S.HPP}:OffLevelOutne, Mode2Value", f"{S.REGS}:kOutneLow, kOutneHigh, kOutneHiZ, kLedFull",
                    f"{S.IPP}:applyOne"],
    }


def build(d):
    f = Fig(
        "output-path",
        "From the registers to the pin",
        alt=("Five stages left to right: full OFF, full ON and the counter decide the channel logic; sleep freezes "
             "it; INVRT and OUTDRV set the pin level; OE high replaces it with the OUTNE level; a table gives the off "
             "level per configuration."),
        width=1200,
        kicker="Hardware",
        subtitle="OE high always gives the level of a channel that is off: the driver chooses OUTNE for that.",
        legend=[("blue", "channel logic"), ("amber", "asleep"), ("violet", "output stage"), ("rose", "OE")],
    )
    W = f.body_w
    gap = 34
    n = 4
    cw = (W - (n - 1) * gap) / n
    stages = [("Channel logic", ["LEDn_OFF_H bit 4: off, whatever else", "LEDn_ON_H bit 4: on",
                                 "else on from the ON count to the OFF count"], "blue"),
              ("Asleep?", ["oscillator off: an output is neither switched on nor off",
                           "it may hold its level (not specified)"], "amber"),
              ("INVRT and OUTDRV", ["INVRT = 1 inverts (OE low only)", "high: driven (totem pole) or",
                                    "high impedance (open drain)"], "violet"),
              ("OE pin", ["low: the level from the left", "high: the OUTNE level, within 60 ns"], "rose")]
    h = max(S.card_h(f, cw, t, lines=ln) for t, ln, _ in stages)
    x = 0.0
    prev = None
    for t, ln, tone in stages:
        b = f.card(x, 0, cw, t, ln, tone=tone, accent=True, h=h)
        if prev is not None:
            f.edge([(prev.r + 4, prev.cy), (b.x - 4, b.cy)], tone="rail")
        prev = b
        x += cw + gap
    o = d["outne"]
    rows = [("Not inverted (Config{}, ServoPreset())", "low", f"{o['low']:02b}", "yes: off for an active-high load"),
            ("Inverted, totem pole (P-type driver)", "high", f"{o['high']:02b}", "no: low turns the load on"),
            ("Inverted, open drain (DirectLedPreset())", "high impedance", f"{o['hiz']:02b}",
             "no: low lights the LED")]
    y = h + 50
    cols = [W * 0.36, W * 0.18, W * 0.12, W - W * 0.66 - 3 * 12]
    heads = ["Configuration", "Channel off at the pin", "OUTNE", "Reset state off?"]
    x = 0.0
    hh = max(S.card_h(f, w, t) for t, w in zip(heads, cols))
    for t, w in zip(heads, cols):
        f.card(x, y, w, t, tone="slate", h=hh)
        x += w + 12
    y += hh + 10
    for r in rows:
        rh = max(S.card_h(f, w, c) for c, w in zip(r, cols))
        x = 0.0
        for i, (c, w) in enumerate(zip(r, cols)):
            f.card(x, y, w, c, tone="violet" if i == 0 else "slate", h=rh, accent=i == 0)
            x += w + 12
        y += rh + 10
    return f
