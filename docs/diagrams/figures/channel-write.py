"""One channel write: a switch-on is four registers in one transfer, a switch-off one byte; what each outcome leaves.

Reads: Top, the transfer the driver sends for a switch-on (SetDuty(), SetPulseWidthUs(), SetFullOn(), SetPwm()): the
control register LEDn_ON_L, then ON_L, ON_H, OFF_L and OFF_H, with auto-increment. With MODE2.OCH = 0, which the
driver always keeps, the bytes reach the output together at the STOP. Below, the outcomes: an acknowledged transfer
updates the driver's copy; a NACK part-way leaves the bytes before it stored and applied at the STOP, so the channel
may hold old, new or mixed values and becomes unknown; an address NACK proves nothing arrived. The band at the
bottom is the switch-off: one data byte, LEDn_OFF_H = 0x10, stored or not, and full OFF takes precedence, so a
switch-off never makes a channel more active, even when it fails; it is sent whatever the driver doubts.
"""

import _source as S
from figkit import Fig


def data():
    fns = S.functions()
    wc = fns["writeChannels"]
    S.order(wc, ["EncodePwm(values[i])", "bus_.Write(reg::ChannelBase(first)", "channels_[first + i] = values[i]",
                 "Error::NoDevice"], "writeChannels()")
    S.need(wc.split("Error::NoDevice", 1)[1].find("uncertain_") > 0, "a failed write makes the channels unknown")
    S.order(fns["switchOn"], ["completeWakeCheck()", "writeChannels("], "switchOn()")
    S.order(fns["afterSwitchOff"], ["if (!written)", "completeWakeCheck()"], "afterSwitchOff()")
    S.need("switchOff(channel, verify)" in fns["applyOne"], "applyOne() sends an off value first")
    S.need("writeByte(reg, reg::kLedFull)" in fns["writeOff"], "a switch-off is one byte to LEDn_OFF_H")
    S.need("writeOff(channel)" in fns["switchOff"], "switchOff() uses the one-byte write")
    base = S.const("kLed0OnL")
    per = S.const("kBytesPerChannel")
    return {
        "led0": S.hexb(base),
        "per": per,
        "full": S.hexb(S.const("kLedFull")),
        "sources": [f"{S.IPP}:applyOne, switchOn, switchOff, afterSwitchOff, writeChannels",
                    f"{S.REGS}:kLed0OnL, kBytesPerChannel, kLedFull"],
    }


def build(d):
    f = Fig(
        "channel-write",
        "One channel write and its outcomes",
        alt=("A row of five bytes, the control register LEDn_ON_L then ON_L, ON_H, OFF_L, OFF_H, applied at the STOP; "
             "below, three outcome cards: acknowledged, NACK part-way, address NACK; at the bottom the one-byte "
             "switch-off."),
        width=1200,
        kicker="Architecture",
        subtitle=(f"Channel n's registers start at {d['led0']} + {d['per']} x n. A switch-on sends all four in one "
                  "transfer; a switch-off sends one byte."),
        legend=[("blue", "bytes on the bus"), ("green", "Ok"), ("rose", "Bus"), ("slate", "NoDevice")],
    )
    W = f.body_w
    names = [("Control register", "LEDn_ON_L address"), ("ON_L", "ON count 7..0"),
             ("ON_H", f"bit 4 full ON ({d['full']}), count 11..8"), ("OFF_L", "OFF count 7..0"),
             ("OFF_H", f"bit 4 full OFF ({d['full']}), count 11..8")]
    gap = 14
    bw = (W - 4 * gap) / 5
    h = max(S.card_h(f, bw, t, lines=[s]) for t, s in names)
    x = 0.0
    boxes = []
    for i, (t, s) in enumerate(names):
        boxes.append(f.card(x, 0, bw, t, [s], tone="blue", accent=i > 0, h=h))
        x += bw + gap
    stop = f.card(0, h + 40, W, "STOP: the four registers reach the output together (MODE2.OCH = 0)",
                  ["a switch-on waits for a passing wake check"], tone="blue")
    for b in boxes:
        f.edge([(b.cx, b.b + 4), (b.cx, stop.y - 4)], tone="blue")
    y = stop.b + 50
    cw = (W - 2 * 24) / 3
    outs = [("Acknowledged: Ok", ["the copy takes the value", "GetChannel() returns it"], "green"),
            ("NACK part-way: Bus", ["the bytes before the NACK are stored and applied at the STOP",
                                    "the channel is unknown until a write or a read succeeds",
                                    "EnableOutputs() refuses meanwhile"], "rose"),
            ("Address NACK: NoDevice", ["nothing arrived", "the copy is kept, nothing unknown",
                                        "needs the port's GetLastFailure()"], "slate")]
    hh = max(S.card_h(f, cw, t, lines=ln) for t, ln, _ in outs)
    x = 0.0
    for t, ln, tone in outs:
        b = f.card(x, y, cw, t, ln, tone=tone, accent=True, h=hh)
        f.edge([(b.cx, stop.b + 4), (b.cx, b.y - 4)], tone=tone)
        x += cw + 24
    f.note(0, y + hh + 40, W, (f"A switch-off is one data byte: LEDn_OFF_H (control register {d['led0']} + "
                               f"{d['per']} x n + 3) = {d['full']}, or ALL_LED_OFF_H = {d['full']} for every channel. "
                               "It is stored or it is not, and full OFF takes precedence over the other bytes, so even "
                               "a failed switch-off never makes a channel more active. It is sent whatever the driver "
                               "doubts, and once started also after a reset was found."),
           tone="green", title="Switch-off")
    return f
