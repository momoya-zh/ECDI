# -*- coding: utf-8 -*-
"""Faithful re-implementation of TextWidget::BuildTextLayout (HEAD 6a3807b)
with FakeTextMeasurer (8 DIP per codepoint, LineHeight 16).
Used only to cross-check the sample tables in the Phase 38 preliminary design."""

CP_W = 8.0

LINE_START_FORBIDDEN = {0x300D,0x300F,0x3011,0x3009,0x300B,0x3001,0x3002,
                        0xFF09,0xFF0C,0xFF01,0xFF1F,0xFF1B,0xFF1A,0x2026}
LINE_END_FORBIDDEN = {0x300C,0x300E,0x3010,0x3008,0x300A,0xFF08}

def is_cjk_body(cp):
    return (0x4E00 <= cp <= 0x9FFF) or (0x3400 <= cp <= 0x4DBF) \
        or (0x20000 <= cp <= 0x2FFFF) or (0x3040 <= cp <= 0x309F) \
        or (0x30A0 <= cp <= 0x30FF)

def is_soft_break_before(cp):
    return cp == 0x20 or cp == 0x2D or is_cjk_body(cp)

def fit_text(cps, start, limit):
    total = len(cps)
    if start >= total:
        return 0, 0.0
    if CP_W > max(0.0, limit):
        return 0, 0.0
    lo, hi = 1, total - start
    while lo < hi:
        mid = lo + (hi - lo + 1)//2
        if CP_W * mid <= max(0.0, limit):
            lo = mid
        else:
            hi = mid - 1
    return lo, CP_W * lo

def find_last_soft_break(cps, line_start, hard_end):
    b = hard_end
    while b > line_start:
        if is_soft_break_before(cps[b-1]):
            return b
        b -= 1
    return hard_end

def build_layout(text, max_width):
    cps = [ord(ch) for ch in text]
    lines = []
    if not cps:
        return [(0,0,0.0,0)], 16.0
    def emit(line_start, line_end, fit_end, fit_width):
        end = line_end
        while end > line_start and cps[end-1] == 0x20:
            end -= 1
        width = fit_width if end == fit_end else CP_W*(end-line_start)
        lines.append((line_start, end-line_start, width, line_end))
    segs = []
    seg_start = 0
    for i, cp in enumerate(cps):
        if cp == 0x0A:
            segs.append((seg_start, i))
            seg_start = i+1
    segs.append((seg_start, len(cps)))
    limit = max(0.0, max_width)
    for s0, s1 in segs:
        if s0 >= s1:
            lines.append((s0, 0, 0.0, s0))
            continue
        scan = s0
        while scan < s1:
            line_start = scan
            fit_cp, fit_w = fit_text(cps, scan, limit)
            hard_end = scan + fit_cp
            if hard_end >= s1:
                emit(line_start, s1, hard_end, fit_w)
                break
            b = find_last_soft_break(cps, line_start, hard_end)
            if b == line_start:
                b = line_start + 1
            while b > line_start and cps[b-1] in LINE_END_FORBIDDEN:
                b -= 1
            if b == line_start:
                b = line_start + 1
            while b < s1 and cps[b] in LINE_START_FORBIDDEN:
                b += 1
            emit(line_start, b, hard_end, fit_w)
            nxt = b
            while nxt < s1 and cps[nxt] == 0x20:
                nxt += 1
            if nxt <= scan:
                nxt = scan + 1
            scan = nxt
    return lines, 16.0*len(lines)

def show(tag, text, w):
    lines, th = build_layout(text, w)
    print("%-4s text=%r cps=%d w=%s -> %d lines totalH=%s" % (tag, text, len(text), w, len(lines), th))
    for i, (s, c, wd, own) in enumerate(lines):
        print("      L%d start=%d draw=[%d,%d) width=%.0f ownedEnd=%d" % (i, s, s, s+c, wd, own))
    # A3 machine check
    bad = []
    for i in range(len(lines)-1):
        gap = lines[i+1][0] - lines[i][3]
        cps = [ord(ch) for ch in text]
        ok = (gap in (0,1)) and ((gap == 1) == (lines[i][3] < len(cps) and cps[lines[i][3]] == 0x0A))
        if not ok:
            bad.append((i, gap))
    print("      A3 gap check: %s" % ("OK" if not bad else "VIOLATION %s" % bad))
    print()

print("=== S1 .. S7 per design §1.4 ===")
show("S1", "ab cd ef", 48)
show("S2", "一二三四五六", 40)
show("S3a(design text)", "ab cd\nef gh", 40)
show("S3b(with space)", "ab cd \nef gh", 40)
show("S3c(width24)", "ab cd\nef gh", 24)
show("S4", "一二三。四", 24)
show("S5", "一二三（四", 32)
show("S6a", "abcdefgh", 26)
show("S6b", "abcdefgh", 40)
show("S7", "secret", 40)
show("tail", "ab\n", 40)

print("=== v1.2 §1.4 additions (S5 re-strung / S5b / S5c / S3@24) ===")
show("S5n", "ab（cd", 24)
show("S5b", "（a", 8)
show("S5c@32", "一二三（四", 32)
show("S3@24", "ab cd \nef gh", 24)

print("=== second width 档 for the 9 samples (A3 18 次全表对账) ===")
SECOND = [("S1", "ab cd ef", 48, 24),
          ("S2", "一二三四五六", 40, 24),
          ("S3", "ab cd \nef gh", 40, 24),
          ("S4", "一二三。四", 24, 16),
          ("S5", "ab（cd", 24, 16),
          ("S5b", "（a", 8, 16),
          ("S5c", "一二三（四", 32, 16),
          ("S6", "abcdefgh", 26, 40),
          ("S7", "secret", 40, 24)]
for tag, text, w1, w2 in SECOND:
    show(tag + "@" + str(w1), text, w1)
    show(tag + "@" + str(w2), text, w2)

print("=== A4 fixture: width=40 padding=0 text='abcdefgh' ===")
for ww in (26.0, 40.0):
    for H in (64, 32, 24):
        lines, th = build_layout("abcdefgh", ww)
        maxscroll = max(0.0, th - H)
        print("  wrapWidth=%.0f H=%d -> n=%d contentH=%.0f maxScroll=%.0f barVisible=%s"
              % (ww, H, len(lines), th, maxscroll, maxscroll > 0))
print()
print("=== A4 as a fixed point under each predicate ===")
def fixed_point(predicate_new, H):
    # iterate to a fixed point (monotone)
    bar = False
    for _ in range(20):
        ww = 40.0 - (12.0 + max(0.0, 2.0)) if (bar or predicate_new) else 40.0
        lines, th = build_layout("abcdefgh", ww)
        new_bar = th - H > 0
        if new_bar == bar:
            return ww, len(lines), th, bar
        bar = new_bar
    return ww, len(lines), th, bar
for H in (64, 32, 24):
    print("  H=%d  NEW(always-deduct)=%s   OLD(visible-only)=%s"
          % (H, fixed_point(True, H), fixed_point(False, H)))
