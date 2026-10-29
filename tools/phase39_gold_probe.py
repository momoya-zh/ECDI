# -*- coding: utf-8 -*-
"""Faithful re-implementation of the Phase 39 code paths, used only to cross-check
the two golden tables in `docs/dev/phase39-utf8-index-detailed-design.md` (§3).

What is re-implemented (and from where):
  * SequenceLength               -- ECDI/src/Core/UTF8.cpp:9-15
  * CodepointIndexToByteOffset   -- ECDI/src/Core/UTF8.cpp:35-43
  * ByteOffsetToCodepointIndex   -- ECDI/src/Core/UTF8.cpp:45-53
  * DecodeFirstCodepoint         -- ECDI/src/Core/UTF8.cpp:55-80
  * TextBox::RecalculateLines    -- ECDI/src/Widget/TextBox.cpp:479-490 (non-wrap branch)
  * TextMeasurer::FitText (default body) -- ECDI/include/ECDI/Render/TextMeasurer.h:68-117
  * FakeTextMeasurer             -- ECDI/src/Tests/TextBoxTests.cpp:35-42 (8 DIP per codepoint)

Scope note: this is a *cross-check*, not a second source of truth -- the shipped
golden values live in TextBoxTests.cpp and the real arbiter is the test suite.
Exit code is non-zero on any mismatch so it can gate a script.

Usage:
  python tools/phase39_gold_probe.py            # summary only
  python tools/phase39_gold_probe.py --verbose  # per-sample rows
"""

from __future__ import annotations

import sys

CP_W = 8.0          # FakeTextMeasurer: 8 DIP per codepoint
LINE_H = 16.0       # FakeTextMeasurer::LineHeight


# ── Core/UTF8.cpp (verbatim behaviour) ──────────────────────────────────────
def seq_len(lead: int) -> int:
    if (lead & 0x80) == 0:
        return 1
    if (lead & 0xE0) == 0xC0:
        return 2
    if (lead & 0xF0) == 0xE0:
        return 3
    if (lead & 0xF8) == 0xF0:
        return 4
    return 1        # continuation byte / 5-6 byte lead: invalid, skip 1


def cp2byte(text: bytes, index: int) -> int:
    offset = 0
    cp = 0
    while offset < len(text) and cp < index:
        offset += seq_len(text[offset])
        cp += 1
    return offset   # out of range -> naturally clamped at len(text)


def byte2cp(text: bytes, limit: int) -> int:
    cp = 0
    pos = 0
    while pos < len(text) and pos < limit:
        pos += seq_len(text[pos])
        cp += 1
    return cp


def decode_first(text: bytes) -> int:
    if not text:
        return 0
    lead = text[0]
    if (lead & 0x80) == 0:
        return lead
    n = seq_len(lead)
    if n == 2 and len(text) >= 2:
        return ((lead & 0x1F) << 6) | (text[1] & 0x3F)
    if n == 3 and len(text) >= 3:
        return ((lead & 0x0F) << 12) | ((text[1] & 0x3F) << 6) | (text[2] & 0x3F)
    if n == 4 and len(text) >= 4:
        return (((lead & 0x07) << 18) | ((text[1] & 0x3F) << 12)
                | ((text[2] & 0x3F) << 6) | (text[3] & 0x3F))
    return lead


# ── FakeTextMeasurer::MeasureText ───────────────────────────────────────────
def measure(text: bytes) -> float:
    return float(sum(1 for b in text if (b & 0xC0) != 0x80)) * CP_W


# ── TextMeasurer::FitText (default body, with R2 already applied) ───────────
def fit_text(text: bytes, start_cp: int, max_width: float) -> tuple[int, float]:
    total_cp = byte2cp(text, len(text))
    if start_cp >= total_cp:
        return (0, 0.0)
    first_end = cp2byte(text, start_cp + 1)
    first = text[cp2byte(text, start_cp):first_end]
    if measure(first) > max(0.0, max_width):
        return (0, 0.0)
    lo, hi = 1, total_cp - start_cp
    while lo < hi:
        mid = lo + (hi - lo + 1) // 2
        b0 = cp2byte(text, start_cp)
        b1 = cp2byte(text, start_cp + mid)
        if measure(text[b0:b1]) <= max(0.0, max_width):
            lo = mid
        else:
            hi = mid - 1
    b0 = cp2byte(text, start_cp)
    b1 = cp2byte(text, start_cp + lo)
    return (lo, measure(text[b0:b1]))


# ── TextBox::RecalculateLines, non-wrap branch ──────────────────────────────
def lines(text: bytes) -> list[tuple[int, int, int, int]]:
    """Returns [(ownBeg, ownEnd, drawBeg, drawEnd), ...] as LineRange/LineDrawRange see them."""
    count = byte2cp(text, len(text))
    starts: list[tuple[int, int, int]] = []      # (startCp, cpCount, ownedEnd)
    start = 0
    for i in range(count):
        b = cp2byte(text, i)
        nb = cp2byte(text, i + 1)
        if decode_first(text[b:nb]) == 0x0A:     # U+000A -- note: overlong also lands here
            starts.append((start, i - start, i))
            start = i + 1
    starts.append((start, count - start, count))
    return [(s, e, s, s + n) for (s, n, e) in starts]


# ── Golden tables (must mirror TextBoxTests.cpp kFitGolden / kLineGolden) ───
FIT_GOLDEN = [
    # (text, startCp, maxWidth, fitCp, width)
    (b"abcdef",               0, 100.0, 6, 48.0),
    (b"abcdef",               2,  24.9, 3, 24.0),
    (b"abcdef",               2,   7.9, 0,  0.0),
    ("一丁丂".encode(),        0,  16.0, 2, 16.0),
    ("一丁丂".encode(),        1, 100.0, 2, 16.0),
    (b"abcdef",               6, 100.0, 0,  0.0),
    (b"",                     0, 100.0, 0,  0.0),
    (b"\xC0\x8A",             0, 100.0, 1,  8.0),
]

LINE_GOLDEN = [
    # (text, [(ownBeg, ownEnd, drawBeg, drawEnd), ...])
    (b"",                  [(0,  0,  0,  0)]),
    (b"\n",                [(0,  0,  0,  0), (1,  1,  1,  1)]),
    (b"\n\n",              [(0,  0,  0,  0), (1,  1,  1,  1), (2,  2,  2,  2)]),
    (b"ab\n",              [(0,  2,  0,  2), (3,  3,  3,  3)]),
    (b"a\r\nb",            [(0,  2,  0,  2), (3,  4,  3,  4)]),
    (b"\x80\x8A",          [(0,  2,  0,  2)]),
    (b"a\xC0",             [(0,  2,  0,  2)]),
    (b"\xC0\x8A",          [(0,  0,  0,  0), (1,  1,  1,  1)]),
    (b"\xE0\x80\x8A",      [(0,  0,  0,  0), (1,  1,  1,  1)]),
    (b"\xF0\x80\x80\x8A",  [(0,  0,  0,  0), (1,  1,  1,  1)]),
    ("\U0001F600 a".encode(), [(0, 3, 0, 3)]),
    (b"abcdefghij",        [(0, 10,  0, 10)]),
    ("一二三四五".encode(),   [(0,  5,  0,  5)]),
]

# Not part of table B (it is overlong *and* has an illegal continuation byte),
# built inline by T39-3 instead.
C0_4A = (b"\xC0\x4A", [(0, 0, 0, 0), (1, 1, 1, 1)])


def main(argv: list[str]) -> int:
    verbose = "--verbose" in argv
    bad = 0

    print("Phase 39 golden probe -- cross-check of TextBoxTests.cpp (Phase 39 batch 0)\n")

    print("table A -- FitText default body (8 samples)")
    for i, (text, sc, mw, fit_cp, width) in enumerate(FIT_GOLDEN, 1):
        got_cp, got_w = fit_text(text, sc, mw)
        ok = got_cp == fit_cp and abs(got_w - width) < 1e-3
        if not ok:
            bad += 1
        if verbose or not ok:
            print("  %s #%d startCp=%d maxW=%.1f -> (%d, %.1f) expect (%d, %.1f)"
                  % ("ok  " if ok else "FAIL", i, sc, mw, got_cp, got_w, fit_cp, width))
    print("  %d/%d ok\n" % (len(FIT_GOLDEN) - bad, len(FIT_GOLDEN)))

    print("table B -- non-wrap line table (13 samples + C0 4A inline)")
    before = bad
    samples = [(t, rows, "B%d" % i) for i, (t, rows) in enumerate(LINE_GOLDEN, 1)]
    samples.append((C0_4A[0], C0_4A[1], "C0 4A"))
    for text, want, name in samples:
        got = lines(text)
        ok = got == want
        if not ok:
            bad += 1
        if verbose or not ok:
            print("  %s %-6s -> %s" % ("ok  " if ok else "FAIL", name, got))
            if not ok:
                print("         expect %s" % (want,))
    print("  %d/%d ok\n" % (len(samples) - (bad - before), len(samples)))

    if bad:
        print("=> %d MISMATCH(ES) -- golden table or implementation drifted" % bad)
        return 1
    print("=> OK (all samples agree)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
