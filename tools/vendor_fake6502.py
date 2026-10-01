#!/usr/bin/env python3
"""Fetch fake6502 (public domain, Mike Chambers) at a pinned commit, patch it,
and write core/fake6502.inc.

Patches applied:
  1. Registers and helper variables become static, so their short names
     (a, x, y, pc, sp, status, ...) stay out of the link namespace.
  2. ADC and SBC get correct NMOS decimal-mode behaviour. The original fails
     the Klaus Dormann functional test in decimal mode.
"""
import hashlib
import pathlib
import sys
import urllib.request

URL = ("https://raw.githubusercontent.com/wutka/kim1-emulator/"
       "cf375bba81b86f69a8b0f72a64c967d87c39886a/fake6502.c")
SHA256 = "c2e985a16ca8425284ff2172feb56862fb02b9ab57faa4d8cbffd6a5d5d2878c"
OUT = pathlib.Path(__file__).resolve().parent.parent / "core" / "fake6502.inc"

STATIC_LINES = [
    "uint16_t pc;",
    "uint8_t sp, a, x, y, status;",
    "uint32_t instructions = 0; //keep track of total instructions executed",
    "uint32_t clockticks6502 = 0, clockgoal6502 = 0;",
    "uint16_t oldpc, ea, reladdr, value, result;",
    "uint8_t opcode, oldstatus;",
    "uint8_t penaltyop, penaltyaddr;",
    "uint8_t callexternal = 0;",
    "void (*loopexternal)();",
]

ADC = '''static void adc() {
    penaltyop = 1;
    value = getvalue();

    if (status & FLAG_DECIMAL) {
        /* NMOS 6502 decimal add: Z from the binary sum, N and V from the
           intermediate result, C from the decimal result. */
        uint16_t carry_in = (uint16_t)(status & FLAG_CARRY);
        uint16_t lo = (uint16_t)(a & 0x0F) + (value & 0x0F) + carry_in;
        uint16_t hi = (uint16_t)(a >> 4) + (value >> 4);
        result = (uint16_t)a + value + carry_in;
        zerocalc(result);
        if (lo > 0x09) { lo += 0x06; hi++; }
        result = (uint16_t)((hi << 4) | (lo & 0x0F));
        signcalc(result);
        overflowcalc(result, a, value);
        if (hi > 0x09) hi += 0x06;
        if (hi > 0x0F) setcarry();
            else clearcarry();
        result = (uint16_t)(((hi << 4) | (lo & 0x0F)) & 0x00FF);
    } else {
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);
    }

    saveaccum(result);
}

'''

SBC = '''static void sbc() {
    uint16_t operand;
    uint16_t carry_in = (uint16_t)(status & FLAG_CARRY);
    penaltyop = 1;
    operand = getvalue();
    value = operand ^ 0x00FF;
    result = (uint16_t)a + value + carry_in;
    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);

    if (status & FLAG_DECIMAL) {
        /* NMOS 6502 decimal subtract: the flags above come from the binary
           result; only the accumulator is decimal-adjusted. */
        int lo = (int)(a & 0x0F) - (int)(operand & 0x0F) + (int)carry_in - 1;
        int hi = (int)(a >> 4) - (int)(operand >> 4);
        if (lo < 0) { lo -= 0x06; hi--; }
        if (hi < 0) hi -= 0x06;
        result = (uint16_t)((((unsigned)hi << 4) | ((unsigned)lo & 0x0F)) & 0x00FF);
    }

    saveaccum(result);
}

'''


def replace_function(text, name, next_name, new_body):
    start = text.index("static void %s() {" % name)
    end = text.index("static void %s() {" % next_name)
    return text[:start] + new_body + text[end:]


def main():
    data = urllib.request.urlopen(URL, timeout=60).read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        sys.exit("fake6502.c hash mismatch\n  expected %s\n  got      %s" % (SHA256, digest))
    text = data.decode("utf-8")

    for line in STATIC_LINES:
        if text.count(line) != 1:
            sys.exit("expected exactly one occurrence of: %s" % line)
        text = text.replace(line, "static " + line)

    text = replace_function(text, "adc", "and", ADC)
    text = replace_function(text, "sbc", "sec", SBC)

    OUT.write_text(text)
    print("wrote %s (%d bytes)" % (OUT, len(text)))


if __name__ == "__main__":
    main()
