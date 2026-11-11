"""Facts the PCA9685 figures read from the driver source, so that a figure fails to build instead of drifting.

Not a figure (the leading underscore keeps it out of the build); the chip figures import it. Every helper raises
when the source no longer has the shape it expects.
"""

import re
from pathlib import Path

from figkit.core import wrap

ROOT = Path(__file__).resolve().parents[3]
REGS = "inc/pca9685_registers.hpp"
TYPES = "inc/pca9685_types.hpp"
HPP = "inc/pca9685.hpp"
IPP = "src/pca9685.ipp"
IFACE = "inc/pca9685_i2c_interface.hpp"


def read(rel: str) -> str:
    return (ROOT / rel).read_text()


def need(cond, what: str):
    if not cond:
        raise SystemExit(f"[figures] the source no longer matches the figure: {what}")
    return cond


def hexb(v: int) -> str:
    return f"0x{v:02X}"


def const(name: str, rel: str = REGS) -> int:
    """The value of `inline constexpr uintN_t <name> = <value>;` (hex, decimal or a shift)."""
    m = need(re.search(rf"inline constexpr u?int\w* {name} =\s*([^;]+);", read(rel)), f"{name} in {rel}")
    v = " ".join(m.group(1).split()).replace("'", "")
    s = re.fullmatch(r"(\d+)U? << (\d+)", v)
    if s:
        return int(s.group(1)) << int(s.group(2))
    return int(v.rstrip("U"), 0)


def functions() -> dict[str, str]:
    """Every PCA9685<Port>:: member defined in the implementation: name -> body."""
    text = read(IPP)
    out = {}
    for m in re.finditer(r"PCA9685<Port>::(\w+)\(", text):
        start = text.index("{", m.end())
        depth, i = 0, start
        while True:
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        out[m.group(1)] = text[start + 1 : i]
    return need(out, "member functions in " + IPP)


def order(body: str, marks: list[str], what: str) -> None:
    pos = [need(body.find(m) + 1, f"{what}: {m}") for m in marks]
    need(pos == sorted(pos), f"{what}: order {marks}")


def errors_in(body: str) -> list[str]:
    seen = []
    for e in re.findall(r"Error::(\w+)", body):
        if e != "Ok" and e not in seen:
            seen.append(e)
    return seen


def card_h(f, w, title, sub="", lines=(), mono=False, tag=""):
    """Height Fig.card() gives this content, to size a row of cards or centre a card on another."""
    k = f.k
    inner = w - 2 * k["pad"] - 6
    tag_w = f.pill_width(tag) + 8 if tag else 0
    t = wrap(title, inner - tag_w, k["card_title"], 700)
    s = wrap(sub, inner, k["small"]) if sub else []
    body = []
    for ln in lines:
        body += wrap(ln, inner, k["mono"] if mono else k["card_body"], 400, mono)
    th, sh, bh = k["card_title"] * 1.3, k["small"] * 1.35, (k["mono"] if mono else k["card_body"]) * 1.45
    return int(k["pad"] + len(t) * th + (len(s) * sh + 2 if s else 0) + (6 + len(body) * bh if body else 0)
               + k["pad"] - 4 + 1)
