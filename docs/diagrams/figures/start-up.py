"""What EnsureInitialized() does, step by step, and the error each failing step returns.

Reads: Top to bottom on the left, the steps of the first EnsureInitialized() call; once it has succeeded a later call
runs the reset check (one read) and returns Ok, or starts again at the top when the chip reset. The configuration and the address are checked before any I/O. Step 1 switches
everything off whatever an earlier run left: OE high, then one-byte ALL_LED_OFF_H writes before and after MODE2,
which sets OCH = 0 so the second one reaches the outputs at its STOP; all four are sent even when one fails. Step 2
reads MODE1, puts the chip to sleep with auto-increment on (EXTCLK by the two-write sequence when asked), writes
PRE_SCALE, which only takes a write asleep, and the SUBADR1 reset sentinel. Step 3 is the identity check: the chip
has no ID register, so MODE1, MODE2, SUBADR1 and PRE_SCALE must read back what was written and every channel full
OFF. Step 4 wakes the oscillator, waits its start-up, runs the wake check (MODE1, MODE2, SUBADR1 and PRE_SCALE once
more) and enables OE. Each card on the right is
the error a step returns; after any failure the driver stays uninitialised and the next call starts at the top.
"""

import re

import _source as S
from figkit import Fig


def data():
    fns = S.functions()
    init = fns["initialize"]
    S.order(init, ["Validate(config_)", "addressValid()", "GetPort().EnsureInitialized()", "enterSafeState()",
                   "configureClock()", "checkIdentity()", "wakeSequence()", "setOutputEnable(GpioSignal::ACTIVE)"],
            "initialize()")
    S.order(fns["EnsureInitialized"], ["if (initialized_)", "CheckForReset()", "initialize()"],
            "EnsureInitialized() checks for a reset when started")
    safe = fns["enterSafeState"]
    S.order(safe, ["GpioSignal::INACTIVE", "writeByte(reg::kAllLedOffH", "writeByte(reg::kMode2", "mode2_ = mode2"],
            "enterSafeState()")
    S.need(safe.count("writeByte(reg::kAllLedOffH") == 2 and safe.count("FirstError") == 3,
           "both ALL_LED_OFF_H writes, each sent whatever failed before")
    clock = fns["configureClock"]
    S.order(clock, ["reg::kMode1, std::span{modes}", "ExternalClockLatched", "reg::kMode1Sleep", "kPreScale",
                    "kSubAdr1Sentinel"], "configureClock()")
    ident = fns["checkIdentity"]
    S.order(ident, ["readModeBytes()", "kSubAdr1Sentinel", "readByte(reg::kPreScale)", "readChannelsInto(0_u8"],
            "checkIdentity()")
    wake = fns["wakeSequence"]
    S.order(wake, ["writeByte(reg::kMode1, mode1_)", "kOscillatorStartUs", "completeWakeCheck()"], "wakeSequence()")
    return {
        "address": [S.hexb(S.const("kMinI2cAddress", S.TYPES)), S.hexb(S.const("kMaxI2cAddress", S.TYPES)),
                    S.hexb(S.const("kAllCallAddress", S.TYPES))],
        "all_off": [S.hexb(S.const("kAllLedOffH")), S.hexb(S.const("kLedFull"))],
        "start_us": S.const("kOscillatorStartUs", S.TYPES),
        "clock_errors": [e for e in S.errors_in(clock) if e != "Bus"],
        "identity_error": S.need("DeviceIdMismatch" in S.errors_in(ident) and "DeviceIdMismatch", "identity error"),
        "sources": [f"{S.IPP}:EnsureInitialized, initialize, enterSafeState, configureClock, checkIdentity, "
                    "wakeSequence, addressValid", f"{S.TYPES}:kMinI2cAddress, kMaxI2cAddress, kAllCallAddress, kOscillatorStartUs",
                    f"{S.REGS}:kAllLedOffH, kLedFull"],
    }


def build(d):
    f = Fig(
        "start-up",
        "What EnsureInitialized() does",
        alt=("Flow chart of EnsureInitialized(): check the configuration and the address, bring the bus up, switch "
             "every output off, sleep and write PRE_SCALE, read everything back as the identity check, wake the "
             "oscillator and check the wake, enable OE; each failing step returns its error."),
        width=1200,
        kicker="Contract",
        subtitle=("Every output off first, whatever an earlier run left. A failed call leaves the driver "
                  "uninitialised, and the next call starts again at the top."),
        legend=[("teal", "your call"), ("blue", "driver steps"), ("slate", "fails, nothing sent"),
                ("rose", "fails"), ("green", "Ok"), ("~solid", "next step")],
    )
    W = f.body_w
    M = 640
    ex = M + 100
    EW = W - ex
    row = 46
    lo, hi, allcall = d["address"]
    reg, full = d["all_off"]

    def exit_card(main, title, lines, tone, label):
        h = S.card_h(f, EW, title, lines=lines)
        box = f.card(ex, main.cy - h / 2, EW, title, lines, tone=tone, accent=True)
        f.edge([(main.r + 4, main.cy), (box.x - 4, main.cy)], tone=tone, label=label)
        return box

    def step(prev, title, lines, label=""):
        box = f.card(0, prev.b + row, M, title, lines, tone="blue", accent=True)
        f.connect(prev, box, label=label)
        return box

    call = f.card(0, 0, M, "EnsureInitialized()", ["started already?"], tone="teal", accent=True)
    exit_card(call, "Ok, or the start-up again", ["the reset check: one read; a reset found: from step 1"],
              "green", "yes")
    s0 = step(call, "Check the configuration and the address",
              [f"Validate(config); address {lo} to {hi}, not {allcall} (LED All Call)"], "no")
    exit_card(s0, "InvalidArgument", ["nothing sent"], "slate", "bad")
    s1 = step(s0, "1  Every output off, from any state",
              ["OE high (with GpioSet())", f"ALL_LED_OFF_H ({reg}) = {full}: one byte, whatever AI holds",
               "MODE2: OCH = 0, the drive, the inversion, OUTNE = the off level",
               f"ALL_LED_OFF_H = {full} again, now applied at the STOP"])
    exit_card(s1, "Bus or NoDevice", ["after all four were sent"], "rose", "fails")
    s2 = step(s1, "2  Sleep and set the frequency",
              ["read MODE1; SLEEP and AI; EXTCLK by two writes when asked",
               "PRE_SCALE (it takes a write only asleep); SUBADR1 = the reset sentinel"])
    exit_card(s2, " or ".join(d["clock_errors"] + ["Bus"]),
              ["ExternalClockLatched: EXTCLK set, internal clock asked",
               "DeviceIdMismatch: MODE1 and the next byte cannot be a PCA9685"], "rose", "fails")
    s3 = step(s2, "3  Identity: read everything back",
              ["MODE1, MODE2, SUBADR1 and PRE_SCALE as written", "all 16 channels full OFF (one 64-byte read)"])
    exit_card(s3, d["identity_error"], ["another part, or a floating or stuck line"], "rose", "differs")
    s4 = step(s3, "4  Wake and check the wake",
              [f"MODE1.SLEEP = 0, wait {d['start_us']} us", "read MODE1, MODE2, SUBADR1, PRE_SCALE; OE low"])
    exit_card(s4, "DeviceReset or WriteMismatch", ["the chip reset, or did not wake"], "rose", "fails")
    ok = f.card(0, s4.b + row, M, "Started: Ok", ["every channel off, the chip running the configuration"],
                tone="green", accent=True)
    f.connect(s4, ok, label="all match")
    return f
