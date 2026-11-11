"""Sleep, Wake() and SetPwmFrequency(): the data sheet's SLEEP and RESTART sequence, and the checks around it.

Reads: Left, the two ways to sleep. SleepMode::OutputsOff writes ALL_LED_OFF first (the orderly shutdown of section
7.3.1.1), so nothing is held and RESTART stays clear; SleepMode::HoldOutputs sleeps with the channels running, so
RESTART sets and an output may hold its level while the oscillator is off. Both read MODE1 and MODE2 first, the
reset check, before MODE1 is written from the driver's copy. Right, the wake: the reset check, MODE1 with SLEEP = 0,
the oscillator start-up wait, then the wake check (MODE1, MODE2 and PRE_SCALE compared with the copy); when RESTART
reads 1 it is written back as 1, which restarts the channels, and because that write clears SLEEP too the wake
check runs once more. SetPwmFrequency() is a sleep, a PRE_SCALE write and read-back, and this wake. The band at the
bottom says what a failed check leaves.
"""

import re

import _source as S
from figkit import Fig


def data():
    fns = S.functions()
    sleep = fns["Sleep"]
    S.order(sleep, ["SleepMode::OutputsOff", "writeAllOff()", "readModes()", "held_maybe_ = true",
                    "reg::kMode1Sleep"], "Sleep()")
    S.order(fns["Wake"], ["readModes()", "wakeSequence()"], "Wake()")
    S.order(fns["wakeSequence"], ["wake_check_pending_ = true", "writeByte(reg::kMode1, mode1_)",
                                  "kOscillatorStartUs", "completeWakeCheck()"], "wakeSequence()")
    check = fns["completeWakeCheck"]
    S.order(check, ["verifyRunning()", "reg::kMode1Sleep", "reg::kMode1Restart"], "completeWakeCheck()")
    S.need(check.count("verifyRunning()") == 2, "the check runs again after the RESTART write")
    freq = fns["SetPwmFrequency"]
    S.order(freq, ["completeWakeCheck()", "readModes()", "reg::kMode1Sleep", "changePrescale(", "wakeSequence()"],
            "SetPwmFrequency()")
    S.need(re.search(r"readByte\(reg::kPreScale\)", fns["changePrescale"]), "PRE_SCALE read back")
    return {
        "start_us": S.const("kOscillatorStartUs", S.TYPES),
        "restart": S.hexb(S.const("kMode1Restart")),
        "sleep": S.hexb(S.const("kMode1Sleep")),
        "sources": [f"{S.IPP}:Sleep, Wake, wakeSequence, completeWakeCheck, verifyRunning, SetPwmFrequency, "
                    "changePrescale", f"{S.REGS}:kMode1Sleep, kMode1Restart", f"{S.TYPES}:kOscillatorStartUs"],
    }


def build(d):
    f = Fig(
        "sleep-restart",
        "Sleep, wake and restart",
        alt=("Two columns: Sleep() with the outputs off or held, and the wake that clears SLEEP, waits for the "
             "oscillator, checks MODE1, MODE2 and PRE_SCALE, then writes RESTART and checks again; a band below "
             "says what a failed check leaves."),
        width=1200,
        kicker="API guide",
        subtitle=("MODE1 is written from the driver's copy only after a read that proves the chip did not reset, "
                  "and every write that clears SLEEP is followed by the wake check."),
        legend=[("teal", "your call"), ("blue", "driver steps"), ("amber", "outputs may hold"), ("green", "running"),
                ("~solid", "next step")],
    )
    W = f.body_w
    gap = 60
    cw = (W - gap) / 2
    rx = cw + gap
    row = 40
    sl = f.card(0, 0, cw, "Sleep(mode)", ["OutputsOff (default) or HoldOutputs"], tone="teal", accent=True)
    off = f.card(0, sl.b + row, cw, "OutputsOff: every channel off",
                 ["ALL_LED_OFF_H: nothing left to hold", "if that write fails: no sleep"], tone="blue", accent=True)
    f.connect(sl, off)
    chk = f.card(0, off.b + row, cw, "Reset check", ["read MODE1, MODE2, SUBADR1: as the copy? twice to act"],
                 tone="blue", accent=True)
    f.connect(off, chk)
    wr = f.card(0, chk.b + row, cw, f"MODE1.SLEEP = 1 ({d['sleep']})", ["the oscillator stops"], tone="blue",
                accent=True)
    f.connect(chk, wr)
    held = f.card(0, wr.b + row, cw, "Asleep",
                  ["OutputsOff: every output off, RESTART clear",
                   "HoldOutputs: RESTART set; an output may hold its level, so a switch-off returns Asleep"],
                  tone="amber", accent=True)
    f.connect(wr, held)
    wk = f.card(rx, 0, cw, "Wake()", ["also the end of SetPwmFrequency()"], tone="teal", accent=True)
    rc = f.card(rx, wk.b + row, cw, "Reset check", ["read MODE1, MODE2, SUBADR1 (section 7.3.1.1, step 1)"],
                tone="blue", accent=True)
    f.connect(wk, rc)
    w1 = f.card(rx, rc.b + row, cw, f"MODE1.SLEEP = 0, wait {d['start_us']} us",
                ["the oscillator start-up (table 5, note 2)", "the wake check is pending from here"],
                tone="blue", accent=True)
    f.connect(rc, w1)
    w2 = f.card(rx, w1.b + row, cw, "Wake check",
                ["read MODE1, MODE2, SUBADR1, PRE_SCALE: as the copy?", "differs twice: DeviceReset"], tone="blue",
                accent=True)
    f.connect(w1, w2)
    w3 = f.card(rx, w2.b + row, cw, f"RESTART reads 1: write it as 1 ({d['restart']})",
                ["the channels that ran restart", "it clears SLEEP too: the wake check once more"], tone="blue",
                accent=True)
    f.connect(w2, w3)
    run = f.card(rx, w3.b + row, cw, "Running",
                 ["the chip holds the driver's MODE2 and prescaler", "no output held"], tone="green", accent=True)
    f.connect(w3, run)
    top = max(held.b, run.b) + 40
    f.note(0, top, W, ("A wake check that fails (a bus error) stays pending: the next call runs it first and a "
                       "switch-on is refused until it passes. A switch-off is still sent. Only an acknowledged wake "
                       "write followed by a check that reads SLEEP = 0 clears the doubt that an output is held; "
                       "Wake() leaves Asleep for Running."),
           tone="slate", title="When a check fails")
    return f
