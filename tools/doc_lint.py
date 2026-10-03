#!/usr/bin/env python3
"""
doc_lint.py -- read-only consistency checks for the ECDI documentation set.

Scope: the *machine-checkable* subset of the ECDI conventions that today live in
the skills ecdi-doc-editing / ecdi-doc-consistency / ecdi-batch-editing.  The
point is to move mechanical verification out of "remember to check" and into a
command, so the skill text only has to carry the rules that need judgement.

Checks
  L01  BOM presence, classified by rule 70.  The DEFECT case is restricted to
       files MSVC compiles (.h / .cpp / .rc): a missing BOM there plus
       non-ASCII means CP936 decoding, C4819 and mojibake.  Any other text file
       with a missing BOM is STYLE at most.
  L02  line endings: pure LF / pure CRLF / MIXED.  MIXED is the defect.
       The convention is per file, not per directory (rule 118) -- this check
       detects, it never enforces one global choice.
  L03  code fence balance: the fence count must be even (rule 29(3)).
  L04  table column consistency per table block, using count('|') -
       count('\\|') exactly as rule 29(3) prescribes.
  L05  unescaped pipes inside table cells that look like |identifier|
       (rule 29, automation note).
  L06  table header and separator merged onto one line (rule 29(3)).
  L07  duplicate headings, scoped to the parent chain (rule 61(4)).  The same
       sub-heading repeated under different sections is legitimate structure;
       only a section duplicated at the same position in the tree is flagged.
  L08  '---' horizontal rules: the lines above and below must be blank,
       otherwise '---' becomes a setext H2 and swallows the paragraph (rule 58(1)).
  L09  consecutive blank lines (rule 58(1), insertion by-products): a run of
       two is INFO, three or more is STYLE.
  L11  revision-record version monotonicity, compared as (major, minor) tuples
       -- never as floats (rules 118(2), 114).
  L12  source line-number references `file.cpp:123` still resolve and are in
       range (rule 114).  Opt-in: --line-refs (noisy on documents that quote
       historical baselines on purpose).
  L13  metric anchors vs measured ground truth (rules 61(5), 115).  Scans only
       the designated current-state carriers -- the root README above
       '## Status', and the 'current scale anchor' callout in docs/README.md.
       Per-phase numbers elsewhere are documented time-point values (rule 22)
       and are deliberately NOT flagged.

Guarantee: this script never writes to, renames or deletes any scanned file.
The only write it can perform is the optional UTF-8 report (--out) or the JSON
dump (--json).  It never calls git rm (rule 72) and never touches the index.

Console output is ASCII-only on purpose: this machine's console code page is
GBK, so echoing document text (CJK punctuation, emoji) to stdout raises
UnicodeEncodeError.  The full report, with document excerpts, goes to --out.

Exit code = number of DEFECT findings (capped at 250); 2 on usage error.
"""

import argparse
import io
import json
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BOM = b"\xef\xbb\xbf"
NON_ASCII = re.compile(rb"[^\x00-\x7f]")

TEXT_EXTS = (".h", ".cpp", ".md", ".rc", ".txt", ".json", ".in", ".py",
             ".yml", ".yaml", ".cmake", ".filters", ".vcxproj", ".user")

SKIP_PARTS = {".git", ".workbuddy", ".workbuddy-ai", ".mimocode", ".idea",
              ".vscode", "learning", "node_modules"}
SKIP_RE = re.compile(
    r"(^|/)(cmake-build-[^/]*|build[^/]*|out|x64|Debug|Release|ipch)(/|$)")

# Files MSVC compiles directly -- the only ones where a missing BOM can become
# C4819 under a CP936 code page (rule 70).
MSVC_SOURCES = (".h", ".cpp", ".rc")

FENCE = re.compile(r"^\s*(```|~~~)")
HEADING = re.compile(r"^(#{1,6})\s+(.*)$")
REV_ENTRY = re.compile(r"^-\s*(?:\*\*)?(v\d+\.\d+)")
# "revision record" heading.  Note this is deliberately NOT a raw string: with
# an r-prefix the \u escapes below would stay literal and the pattern would
# never match.  The optional leading section number covers the ledgers, which
# write "## 8. revision-record" while the older phase docs write it bare.
REV_HEADING = re.compile(
    "^#{1,6}\\s*(?:\\d+(?:\\.\\d+)*\\.?\\s*)?\u4fee\u8ba2")

DEFECT, STYLE, INFO = "DEFECT", "STYLE", "INFO"
SEV_ORDER = {DEFECT: 0, STYLE: 1, INFO: 2}


# --------------------------------------------------------------------------
# discovery
# --------------------------------------------------------------------------

def die(msg):
    sys.stderr.write("doc_lint: %s\n" % msg)
    raise SystemExit(2)


def tracked_files():
    """Every file git knows about -- the same scope rule 70's audit uses."""
    proc = subprocess.run(["git", "ls-files", "-z"], cwd=REPO,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        die("git ls-files failed -- run this from inside the ECDI work tree "
            "(%s)" % REPO)
    return [p for p in proc.stdout.decode("utf-8", "replace").split("\0") if p]


def wanted(rel):
    if any(part in SKIP_PARTS for part in rel.split("/")):
        return False
    if SKIP_RE.search(rel):
        return False
    return rel.lower().endswith(TEXT_EXTS)


def read_bytes(rel):
    with open(os.path.join(REPO, rel), "rb") as handle:
        return handle.read()


# --------------------------------------------------------------------------
# findings
# --------------------------------------------------------------------------

class Report(object):
    def __init__(self):
        self.items = []
        self.checked = 0
        self.metrics = {}

    def add(self, sev, check, path, line, msg, detail=""):
        self.items.append({"severity": sev, "check": check, "path": path,
                           "line": line, "message": msg, "detail": detail})

    def counted(self, name, value):
        self.metrics[name] = value

    def defects(self):
        return [i for i in self.items if i["severity"] == DEFECT]


# --------------------------------------------------------------------------
# per-file checks
# --------------------------------------------------------------------------

def check_bom(report, rel, raw):
    if raw.startswith(BOM):
        return
    non_ascii = bool(NON_ASCII.search(raw))
    # Rule 70's DEFECT classification is about MSVC reading the source as CP936
    # and emitting C4819, so it only applies to files MSVC actually compiles.
    # A Python file holding CJK text is decoded as UTF-8 by the interpreter
    # (PEP 3120) and carries no such risk -- there the BOM is style, not defect.
    if non_ascii and rel.lower().endswith(MSVC_SOURCES):
        report.add(DEFECT, "L01", rel, 1,
                   "missing UTF-8 BOM and file contains non-ASCII "
                   "(rule 70: MSVC compiles this under CP936)")
    elif not raw.strip():
        report.add(INFO, "L01", rel, 1, "empty file")
    elif non_ascii:
        report.add(STYLE, "L01", rel, 1,
                   "missing UTF-8 BOM (non-ASCII, but not MSVC-compiled)")
    else:
        report.add(STYLE, "L01", rel, 1,
                   "missing UTF-8 BOM (pure ASCII, style only)")


def check_eol(report, rel, raw):
    crlf = raw.count(b"\r\n")
    lf = raw.count(b"\n") - crlf
    cr = raw.count(b"\r") - crlf
    if crlf and lf:
        report.add(DEFECT, "L02", rel, 0,
                   "mixed line endings: CRLF=%d bare LF=%d" % (crlf, lf))
    if cr:
        report.add(DEFECT, "L02", rel, 0, "bare CR (no LF) x%d" % cr)
    if rel.lower().endswith(".md"):
        report.add(INFO, "L02", rel, 0,
                   "eol=%s (CRLF=%d, LF=%d)"
                   % ("CRLF" if crlf and not lf else
                      "LF" if lf and not crlf else "none", crlf, lf))


def scan_markdown(report, rel, text, opts):
    lines = text.split("\n")
    total = len(lines)

    # ---- fence state (L03), plus an "inside fence" mask ----------------
    markers = [i for i, line in enumerate(lines) if FENCE.match(line)]
    if len(markers) % 2:
        report.add(DEFECT, "L03", rel, markers[-1] + 1,
                   "unbalanced code fences: %d markers (must be even)"
                   % len(markers))

    inside = [False] * total
    open_state = False
    for i, line in enumerate(lines):
        if FENCE.match(line):
            inside[i] = False          # the marker line itself
            open_state = not open_state
        else:
            inside[i] = open_state

    # ---- L04/L05/L06: tables ------------------------------------------
    def pipes(value):
        return value.count("|") - value.count("\\|")

    i = 0
    while i < total:
        row = lines[i].strip()
        if not inside[i] and row.startswith("|") and pipes(row) >= 2:
            j = i
            while (j < total and not inside[j]
                   and lines[j].strip().startswith("|")
                   and pipes(lines[j]) >= 2):
                j += 1
            expected = pipes(lines[i])
            for k in range(i, j):
                actual = pipes(lines[k])
                if actual != expected:
                    report.add(DEFECT, "L04", rel, k + 1,
                               "table row has %d columns, header has %d"
                               % (actual, expected),
                               lines[k].strip()[:200])
                loose = re.findall(r"(?<!\\)\|[A-Za-z0-9_]+\|", lines[k])
                if loose:
                    report.add(DEFECT, "L05", rel, k + 1,
                               "unescaped pipe pair inside a cell: %s"
                               % ", ".join(loose),
                               lines[k].strip()[:200])
                cells = lines[k].strip().strip("|").split("|")
                dashed = [c for c in cells if re.fullmatch(r":?-{2,}:?", c.strip())]
                filled = [c for c in cells
                          if c.strip() and not re.fullmatch(r"[-: ]*", c.strip())]
                if dashed and filled:
                    report.add(DEFECT, "L06", rel, k + 1,
                               "header and separator merged on one line",
                               lines[k].strip()[:200])
            i = j
        else:
            i += 1

    # ---- L07: duplicate headings --------------------------------------
    # Uniqueness is scoped to the parent chain on purpose: an identical
    # sub-heading repeated under different sections (for instance "decision
    # points" under each of R1..R5) is legitimate structure, not damage.  Only
    # a duplicated section sitting at the same position in the tree is a real
    # signal -- which is what an accidental block duplication looks like.
    seen = {}
    stack = []
    for i, line in enumerate(lines):
        if inside[i]:
            continue
        match = HEADING.match(line)
        if not match:
            continue
        level = len(match.group(1))
        title = match.group(2).strip()
        while stack and stack[-1][0] >= level:
            stack.pop()
        key = (tuple(t for _lvl, t in stack), level, title)
        if key in seen:
            report.add(STYLE, "L07", rel, i + 1,
                       "duplicate heading under the same parent (first at "
                       "line %d): %s" % (seen[key], title[:80]))
        else:
            seen[key] = i + 1
        stack.append((level, title))

    # ---- L08: setext '---' boundaries ---------------------------------
    for i, line in enumerate(lines):
        if inside[i] or line.strip() != "---":
            continue
        above = lines[i - 1].strip() if i > 0 else ""
        below = lines[i + 1].strip() if i + 1 < total else ""
        if above or below:
            report.add(DEFECT, "L08", rel, i + 1,
                       "'---' is not isolated by blank lines -> becomes a "
                       "setext H2 (rule 58(1))")

    # ---- L09: consecutive blank lines ---------------------------------
    # Rule 58(1) treats a doubled blank line as the fingerprint of a botched
    # insertion.  Measured repo-wide it is endemic, so a pair stays INFO and
    # only three or more is raised to STYLE, which keeps the actionable list
    # short enough to actually act on.
    run = 0
    for i, line in enumerate(lines):
        if line.strip():
            if run >= 2:
                report.add(STYLE if run >= 3 else INFO, "L09",
                           rel, i - run + 1,
                           "%d consecutive blank lines" % run)
            run = 0
        else:
            run += 1

    # ---- L11: revision record ------------------------------------------
    # Two distinct defects live here, and they need different messages:
    #   (a) an entry that landed outside the revision section entirely -- the
    #       documented failure mode where a block insertion splits a section;
    #   (b) entries inside the section whose version order is broken, in either
    #       direction, because this repo has both "new first" and "old first"
    #       ledgers.  Direction is read from the section, never assumed.
    rev_at = None
    rev_level = 0
    for i, line in enumerate(lines):
        if REV_HEADING.match(line):
            rev_at = i
            rev_level = len(line) - len(line.lstrip("#"))
    if rev_at is not None:
        rev_end = total
        for i in range(rev_at + 1, total):
            match = HEADING.match(lines[i])
            if match and len(match.group(1)) <= rev_level:
                rev_end = i
                break

        entries = []
        for i in range(rev_at + 1, total):
            match = REV_ENTRY.match(lines[i])
            if not match:
                continue
            if i >= rev_end:
                report.add(STYLE, "L11", rel, i + 1,
                           "%s sits outside the revision section (which ends "
                           "at line %d)" % (match.group(1), rev_end + 1))
                continue
            major, minor = match.group(1)[1:].split(".")
            entries.append((int(major), int(minor), i + 1))

        if len(entries) >= 2:
            ascending = entries[0][:2] < entries[-1][:2]
            for a, b in zip(entries, entries[1:]):
                bad = (b[:2] < a[:2]) if ascending else (b[:2] > a[:2])
                if bad:
                    report.add(STYLE, "L11", rel, b[2],
                               "revision entries out of order (%s): v%d.%d at "
                               "line %d precedes v%d.%d at line %d"
                               % ("ascending" if ascending else "descending",
                                  a[0], a[1], a[2], b[0], b[1], b[2]))

    # ---- L12: line-number references ----------------------------------
    if opts.line_refs:
        check_line_refs(report, rel, lines, inside, opts)

    # ---- L13: metric anchors ------------------------------------------
    check_metrics(report, rel, lines, opts)


REF_RE = re.compile(
    r"([A-Za-z0-9_][A-Za-z0-9_./\\-]*\.(?:h|cpp|md|py|json|yml|yaml|txt|rc|in))"
    r":(\d+)(?:-(\d+))?")

# Path prefixes that belong to the standard library or the toolchain rather
# than to this repository, so a missing file under them is not a rotten
# reference (rule 114).
EXTERNAL_PREFIXES = ("bits/", "sys/", "detail/", "ext/", "c++/", "win32/",
                     "boost/", "fmt/")


def build_basename_index():
    index = {}
    for rel in tracked_files():
        if not wanted(rel):
            continue
        index.setdefault(os.path.basename(rel), []).append(rel)
    return index


def check_line_refs(report, rel, lines, inside, opts):
    index = opts.basename_index
    for i, line in enumerate(lines):
        if inside[i]:
            continue
        for match in REF_RE.finditer(line):
            target, first = match.group(1), int(match.group(2))
            last = int(match.group(3)) if match.group(3) else first
            norm = target.replace("\\", "/")
            candidates = []
            if "/" in norm:
                for prefix in ("", "docs/", "ECDI/", "ECDI/src/",
                               "ECDI/include/", "ECDI/include/ECDI/",
                               "examples/"):
                    candidate = prefix + norm
                    if os.path.exists(os.path.join(REPO, candidate)):
                        candidates.append(candidate)
                if not candidates:
                    # The docs habitually cite only the tail of a nested header
                    # path ("Core/Logger.h", "KeyBoard/KeyEvent.h"), which no
                    # fixed prefix list can resolve -- fall back to a path
                    # suffix match over every tracked file.
                    tail = "/" + norm
                    candidates = [p for p in opts.all_paths if p.endswith(tail)]
            else:
                candidates = index.get(os.path.basename(target), [])
            if not candidates:
                # Headers shipped with the standard library or the toolchain are
                # not repo files and must not be reported as rotten references.
                if "/" in norm and not norm.startswith(EXTERNAL_PREFIXES):
                    report.add(STYLE, "L12", rel, i + 1,
                               "line reference points at a file that does not "
                               "exist: %s" % target)
                continue
            if len(candidates) > 1:
                report.add(INFO, "L12", rel, i + 1,
                           "ambiguous file name in reference: %s (%d matches)"
                           % (target, len(candidates)))
                continue
            path = os.path.join(REPO, candidates[0])
            with open(path, "rb") as handle:
                count = handle.read().count(b"\n") + 1
            if last > count:
                report.add(STYLE, "L12", rel, i + 1,
                           "line reference out of range: %s:%d (file has %d "
                           "lines)" % (target, last, count))


METRIC_SPECS = (
    ("tests", ("cases", "\u7528\u4f8b")),
    ("headers", ("public headers", "Public \u5934", "\u516c\u5171\u5934")),
    ("docs", ("design documents", "\u8bbe\u8ba1\u6587\u6863")),
)


def metric_zones(rel, lines):
    """Only the designated current-state carriers -- see the module docstring."""
    if rel == "README.md":
        cut = len(lines)
        for i, line in enumerate(lines):
            if line.startswith("## Status"):
                cut = i
                break
        return list(range(cut))
    if rel == "docs/README.md":
        zones = []
        for i, line in enumerate(lines):
            if "\u89c4\u6a21\u951a\u70b9" in line or "\u5f53\u524d\u89c4\u6a21" in line:
                zones.append(i)
                j = i + 1
                while j < len(lines) and lines[j].startswith(">"):
                    zones.append(j)
                    j += 1
        return zones
    return []


def check_metrics(report, rel, lines, opts):
    zones = metric_zones(rel, lines)
    if not zones:
        return
    for i in zones:
        line = lines[i]
        numbers = set(int(n) for n in re.findall(r"\d{2,4}", line))
        if not numbers:
            continue          # nothing numeric claimed here, nothing to check
        for name, keywords in METRIC_SPECS:
            if not any(k in line for k in keywords):
                continue
            expected = opts.metrics.get(name)
            if expected is None or expected in numbers:
                continue
            report.add(DEFECT, "L13", rel, i + 1,
                       "metric '%s' anchor disagrees with measured value %d "
                       "(numbers on this line: %s)"
                       % (name, expected,
                          ", ".join(str(n) for n in sorted(numbers)) or "none"),
                       line.strip()[:300])


# --------------------------------------------------------------------------
# ground truth
# --------------------------------------------------------------------------

def measure(report):
    tests = 0
    tests_dir = os.path.join(REPO, "ECDI", "src", "Tests")
    pattern = "GetTestRegistry().Add"
    for name in sorted(os.listdir(tests_dir)):
        if not name.endswith(".cpp"):
            continue
        with open(os.path.join(tests_dir, name), "rb") as handle:
            tests += handle.read().count(pattern.encode("ascii"))

    header_root = os.path.join(REPO, "ECDI", "include", "ECDI")
    headers = 0
    for root, _dirs, files in os.walk(header_root):
        for name in files:
            if name.endswith(".h") and name != "version.h":
                headers += 1

    docs = 0
    for root, _dirs, files in os.walk(os.path.join(REPO, "docs")):
        docs += sum(1 for name in files if name.endswith(".md"))

    report.counted("tests", tests)
    report.counted("headers", headers)
    report.counted("docs", docs)
    return {"tests": tests, "headers": headers, "docs": docs}


# --------------------------------------------------------------------------
# output
# --------------------------------------------------------------------------

def ascii_safe(value):
    return "".join(ch if 32 <= ord(ch) < 127 else "?" for ch in value)


def write_report(report, path, ground):
    order = sorted(report.items,
                   key=lambda it: (SEV_ORDER[it["severity"]], it["check"],
                                   it["path"], it["line"]))
    with io.open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("ECDI doc lint report\n")
        handle.write("measured: tests=%d headers=%d docs=%d\n\n"
                     % (ground["tests"], ground["headers"], ground["docs"]))
        for item in order:
            handle.write("[%s] %s %s:%d  %s\n"
                         % (item["severity"], item["check"], item["path"],
                            item["line"], item["message"]))
            if item["detail"]:
                handle.write("        %s\n" % item["detail"])
        handle.write("\n%d findings\n" % len(order))


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Read-only consistency checks for the ECDI docs.")
    parser.add_argument("--out", metavar="FILE",
                        help="write the full UTF-8 report here")
    parser.add_argument("--json", metavar="FILE",
                        help="write findings as JSON here")
    parser.add_argument("--line-refs", action="store_true",
                        help="also check file.cpp:123 references (L12)")
    parser.add_argument("--select", metavar="CSV",
                        help="run only these checks, e.g. L01,L04")
    parser.add_argument("--quiet", action="store_true",
                        help="suppress the per-check summary lines")
    opts = parser.parse_args(argv)

    if not os.path.isdir(os.path.join(REPO, ".git")):
        die("no .git under %s" % REPO)

    report = Report()
    ground = measure(report)
    opts.metrics = ground
    files = [rel for rel in tracked_files() if wanted(rel)]
    if opts.line_refs:
        opts.basename_index = build_basename_index()
        opts.all_paths = files
    else:
        opts.basename_index = {}
        opts.all_paths = []

    selected = None
    if opts.select:
        selected = set(s.strip().upper() for s in opts.select.split(",") if s.strip())

    for rel in files:
        report.checked += 1
        raw = read_bytes(rel)
        text = raw.decode("utf-8-sig", "replace")
        check_bom(report, rel, raw)
        check_eol(report, rel, raw)
        if rel.lower().endswith(".md"):
            scan_markdown(report, rel, text, opts)

    if selected:
        report.items = [i for i in report.items if i["check"] in selected]

    # ---- console summary (ASCII only) ---------------------------------
    by_check = {}
    for item in report.items:
        by_check.setdefault(item["check"], []).append(item)

    sys.stdout.write("ECDI doc lint\n")
    sys.stdout.write("  scanned %d tracked text files\n" % report.checked)
    sys.stdout.write("  measured tests=%d headers=%d docs=%d\n"
                     % (ground["tests"], ground["headers"], ground["docs"]))

    defects = report.defects()
    sys.stdout.write("  findings: %d defect, %d style, %d info\n"
                     % (len(defects),
                        len([i for i in report.items if i["severity"] == STYLE]),
                        len([i for i in report.items if i["severity"] == INFO])))

    if not opts.quiet:
        for check in sorted(by_check):
            items = by_check[check]
            worst = min(SEV_ORDER[i["severity"]] for i in items)
            label = [k for k, v in SEV_ORDER.items() if v == worst][0]
            sys.stdout.write("    %s  %-4s %3d finding(s)\n"
                             % (label, check, len(items)))

    showing = [i for i in sorted(
        report.items,
        key=lambda it: (SEV_ORDER[it["severity"]], it["check"], it["path"],
                        it["line"])) if i["severity"] != INFO][:40]
    if showing:
        sys.stdout.write("\n  first %d non-info findings:\n" % len(showing))
        for item in showing:
            sys.stdout.write("    %-6s %-4s %s:%d  %s\n"
                             % (item["severity"], item["check"], item["path"],
                                item["line"], ascii_safe(item["message"])))

    if opts.out:
        write_report(report, opts.out, ground)
        sys.stdout.write("\n  full report -> %s\n" % opts.out)

    if opts.json:
        with io.open(opts.json, "w", encoding="utf-8", newline="\n") as handle:
            json.dump({"measured": ground, "findings": report.items},
                      handle, ensure_ascii=False, indent=1)
        sys.stdout.write("  json -> %s\n" % opts.json)

    return min(len(defects), 250)


if __name__ == "__main__":
    raise SystemExit(main())
