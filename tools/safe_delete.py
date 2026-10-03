#!/usr/bin/env python3
"""
safe_delete.py -- the one sanctioned way for an agent to remove files in ECDI.

Why this exists
  The worry is not "an agent deletes the wrong file", it is "an agent is quiet
  for a while and then things are suddenly gone".  So the default is a dry run,
  wildcards are refused outright, a single run is capped at the project's own
  batch size (<= 10 items, then verify), anything outside the workspace needs an
  explicit --allow-outside, and every run is written to an audit log.  Nothing
  here is clever; all of it is meant to be boring and predictable.

Modes
  (default)         dry run -- print exactly what would happen, touch nothing.
  --apply           send each target to the Recycle Bin.
  --to-trash DIR    move each target into DIR/<UTC timestamp>/, keeping its
                    path relative to the root.  This mode never deletes;
                    without --apply it is still only a dry run.
  --audit-only      parse the Recycle Bin $I cache and print the original paths
                    it records.  Deletes nothing.  Given paths are used only to
                    pick which drives to scan (--drive C: when none are given),
                    so they need not still exist.

Three facts that are easy to get wrong (each was measured in this repo)
  1. pFrom of SHFILEOPSTRUCTW must be DOUBLE-NUL terminated, and several paths
     inside it are separated by a single NUL: build it with
     ctypes.create_unicode_buffer("path\\0") for one path, or with
     "\\0".join(paths) plus a trailing "\\0" for many.  A single trailing NUL
     truncates the list, and the call can then quietly do nothing at all.
  2. SHFILEOPSTRUCTW.fFlags is a WORD (ctypes.c_uint16), NOT a UINT.  Declaring
     it as UINT shifts every field after it by two bytes.  Field order:
     hwnd, wFunc, pFrom, pTo, fFlags, fAnyOperationsAborted, hNameMappings,
     lpszProgressTitle.
  3. The return value of SHFileOperationW is NOT the verdict.  It has been seen
     returning 2 (ERROR_FILE_NOT_FOUND) while the deletion had in fact
     succeeded.  Success is judged only by re-stat-ing every target after the
     call; the rc is printed for the record and decides nothing.

Refusals -- exit 1, nothing touched
  an argument containing * ? [ ]  |  a target resolving outside the root
  (unless --allow-outside)  |  a target that does not exist (except in
  --audit-only)  |  a drive root, the root itself, or any directory containing
  .git  |  the same path twice after normalisation  |  more targets than the
  cap (default 10, raise with --max N).

Console output is ASCII-only on purpose: this machine's console code page is
GBK, so printing CJK from a path to stdout raises UnicodeEncodeError.  Paths
are sanitised with ascii_safe(), the same way doc_lint.py does it.

Writes: the Recycle Bin (--apply), the --to-trash destination, and the audit
log <root>/.workbuddy/tmp/safe-delete.log, which is appended on EVERY run
including dry runs -- the requirement is one line per run, so the log is the
only write a dry run performs.  This script never calls git rm and never
touches the git index; git ls-files and git check-ignore are read-only queries
used to report the project's "ignored and untracked" safety criterion.

Exit codes: 0 = success or dry run completed; 1 = refused / usage error;
2 = the operation ran but at least one target survived.
"""

import argparse
import ctypes
import datetime
import io
import os
import re
import shutil
import subprocess
import sys

if os.name != "nt":                 # shell32, and the $I/$R cache, are Windows
    sys.stderr.write("safe_delete: Windows only (needs shell32 and the "
                     "Recycle Bin)\n")
    raise SystemExit(1)

from ctypes import wintypes

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG_DIR = ".workbuddy"
LOG_NAME = "safe-delete.log"

WILDCARDS = "*?[]"
DEFAULT_MAX = 10

FO_DELETE = 3
FOF_SILENT = 0x0004
FOF_NOCONFIRMATION = 0x0010
FOF_ALLOWUNDO = 0x0040
FOF_NOERRORUI = 0x0400
DELETE_FLAGS = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI

V1_PATH_CHARS = 260                 # pre-Vista $I fallback, see parse_record
DRIVE_RE = re.compile(r"^[A-Za-z]:$")


class SHFILEOPSTRUCTW(ctypes.Structure):
    """The documented layout, with fFlags as WORD -- see fact 2 above."""

    _fields_ = [
        ("hwnd", wintypes.HWND),
        ("wFunc", wintypes.UINT),
        ("pFrom", wintypes.LPCWSTR),
        ("pTo", wintypes.LPCWSTR),
        ("fFlags", ctypes.c_uint16),
        ("fAnyOperationsAborted", wintypes.BOOL),
        ("hNameMappings", ctypes.c_void_p),
        ("lpszProgressTitle", wintypes.LPCWSTR),
    ]


# --------------------------------------------------------------------------
# small shared helpers
# --------------------------------------------------------------------------

def die(msg, code=1):
    sys.stderr.write("safe_delete: %s\n" % ascii_safe(msg))
    raise SystemExit(code)


def ascii_safe(value):
    """Non-ASCII to '?' -- the console code page here is GBK (same as doc_lint)."""
    return "".join(ch if 32 <= ord(ch) < 127 else "?" for ch in value)


def say(line=""):
    sys.stdout.write(ascii_safe(line) + "\n")


def utc_stamp():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def utc_iso():
    return datetime.datetime.now(datetime.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%SZ")


def is_within(path, root):
    """True when path is root itself or sits underneath it (case-insensitive)."""
    path = os.path.normcase(os.path.normpath(path))
    root = os.path.normcase(os.path.normpath(root))
    return path == root or path.startswith(root + os.sep)


def key_of(path):
    return os.path.normcase(os.path.normpath(path))


# --------------------------------------------------------------------------
# audit log -- one line per run, appended even for dry runs
# --------------------------------------------------------------------------

def append_log(root, mode, outcomes):
    line = "%s\tmode=%s\t%s\n" % (
        utc_iso(), mode,
        " ; ".join("%s=%s" % (path, outcome) for path, outcome in outcomes)
        or "paths=0")
    directory = os.path.join(root, LOG_DIR, "tmp")
    try:
        os.makedirs(directory, exist_ok=True)
        with io.open(os.path.join(directory, LOG_NAME), "a", encoding="utf-8",
                     newline="\n") as handle:
            handle.write(line)
    except OSError as exc:          # a broken log must never mask the refusal
        sys.stderr.write("safe_delete: cannot append %s (%s)\n"
                         % (ascii_safe(os.path.join(directory, LOG_NAME)),
                            ascii_safe(str(exc))))


def refuse(root, mode, msg, outcomes=()):
    append_log(root, "refused(%s)" % mode,
               list(outcomes) or [("-", "refused: %s" % msg)])
    die(msg)


# --------------------------------------------------------------------------
# git facts -- read-only queries, never git rm (rule 72)
# --------------------------------------------------------------------------

def run_git(args):
    try:
        return subprocess.run(["git"] + args, cwd=REPO,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except OSError:
        return None


def git_facts(path):
    """(tracked, ignored) as printable strings.  'ignored AND untracked' is the
    project's criterion for safely deletable, so both are always reported."""
    if not os.path.isdir(os.path.join(REPO, ".git")):
        return "unknown (no .git)", "unknown (no .git)"
    if is_within(path, REPO):
        spec = os.path.relpath(path, REPO).replace("\\", "/")
    else:
        spec = path.replace("\\", "/")

    proc = run_git(["ls-files", "--error-unmatch", "--", spec])
    tracked = "unknown" if proc is None else ("yes" if proc.returncode == 0
                                              else "no")

    proc = run_git(["check-ignore", "-v", "--", spec])
    if proc is None:
        ignored = "unknown"
    elif proc.returncode == 0:
        first = proc.stdout.decode("utf-8", "replace").strip().splitlines()
        ignored = "yes (%s)" % (first[0][:140] if first else "no rule shown")
    else:
        ignored = "no"
    return tracked, ignored


# --------------------------------------------------------------------------
# target validation
# --------------------------------------------------------------------------

def resolve_targets(raw, root, allow_outside, cap, audit, mode):
    """Normalise, refuse, and return [(display_path, real_path), ...]."""
    if len(raw) > cap:
        refuse(root, mode,
               "batch cap is %d but %d targets were given (raise with --max N)"
               % (cap, len(raw)))

    resolved = []
    seen = {}
    for item in raw:
        for bad in WILDCARDS:
            if bad in item:
                refuse(root, mode,
                       "wildcard %r in argument -- only literal paths are "
                       "accepted, nothing is ever expanded: %s" % (bad, item))
        target = os.path.abspath(item)
        real = os.path.realpath(target)
        if not is_within(real, root) and not allow_outside:
            refuse(root, mode,
                   "outside the root -- refusing: %s (root is %s; pass "
                   "--allow-outside to override)" % (target, root))
        k = key_of(real)
        if k in seen:
            refuse(root, mode,
                   "same path given twice after normalisation: %r and %r are "
                   "both %s" % (seen[k], item, target))
        seen[k] = item
        if not audit:
            guard_target(target, real, root, mode)
        resolved.append((target, real))
    return resolved


def guard_target(target, real, root, mode):
    """The per-target refusals: drive root, the root itself, anything holding
    a .git directory, and non-existence."""
    drive, tail = os.path.splitdrive(real)
    if tail in ("\\", "/"):
        refuse(root, mode, "that is a drive root, refusing: %s" % target)
    if key_of(real) == key_of(root):
        refuse(root, mode, "that is the workspace root itself, refusing: %s"
               % target)
    gitdir = os.path.join(root, ".git")
    if (key_of(gitdir) == key_of(real) or is_within(gitdir, real)
            or os.path.exists(os.path.join(real, ".git"))):
        refuse(root, mode,
               "target contains a .git directory, refusing: %s" % target)
    if not os.path.lexists(target):
        refuse(root, mode, "target does not exist: %s" % target)


# --------------------------------------------------------------------------
# the delete itself -- Recycle Bin, judged by the filesystem
# --------------------------------------------------------------------------

def shell_delete(path):
    """Send one path to the Recycle Bin.  Returns (rc, aborted).

    pFrom is DOUBLE-NUL terminated (fact 1) and the rc is returned only so it
    can be printed -- it is never the verdict (fact 3)."""
    buffer = ctypes.create_unicode_buffer(path + "\0")
    op = SHFILEOPSTRUCTW()
    op.hwnd = None
    op.wFunc = FO_DELETE
    op.pFrom = ctypes.cast(buffer, wintypes.LPCWSTR)
    op.pTo = None
    op.fFlags = DELETE_FLAGS
    op.fAnyOperationsAborted = False
    op.hNameMappings = None
    op.lpszProgressTitle = None
    shell32 = ctypes.windll.shell32
    shell32.SHFileOperationW.argtypes = [ctypes.POINTER(SHFILEOPSTRUCTW)]
    shell32.SHFileOperationW.restype = ctypes.c_int
    rc = int(shell32.SHFileOperationW(ctypes.byref(op)))
    return rc, bool(op.fAnyOperationsAborted)


DELETE_CALL_NOTE = (
    "one SHFileOperationW call per target, so the rc and the re-stat can be "
    "reported per path")


def check_apply_mode(targets, root, opts):
    """Delete (default) or move (--to-trash).  Returns the outcome list."""
    outcomes = []
    survivors = 0
    for target, real in targets:
        if opts.to_trash:
            outcome, survived = move_target(target, real, root, opts)
        else:
            outcome, survived = delete_target(target, opts)
        outcomes.append((target, outcome))
        survivors += 1 if survived else 0
    return outcomes, survivors


def delete_target(target, opts):
    if not opts.apply:
        say("    would delete (Recycle Bin, never a hard delete)")
        return "dry-run", False

    rc, aborted = shell_delete(target)
    say("    SHFileOperationW rc=%d fAnyOperationsAborted=%s" % (rc, aborted))
    gone = not os.path.lexists(target)
    if gone:
        say("    filesystem: GONE")
        if rc != 0:
            say("    note: rc=%d is a misleading non-zero code -- the target is "
                "gone (fact 3)" % rc)
        return "deleted(rc=%d)" % rc, False
    say("    filesystem: STILL PRESENT")
    return "survived(rc=%d)" % rc, True


def placement(target, root):
    """Where a moved target lands under the timestamp directory."""
    try:
        rel = os.path.relpath(target, root)
    except ValueError:                          # different drive
        rel = None
    if not rel or rel == ".." or rel.startswith(".." + os.sep):
        drive, tail = os.path.splitdrive(target)
        rel = os.path.join("_outside", (drive.rstrip(":") or "x") + tail)
    return rel


def move_target(target, real, root, opts):
    dest_dir = os.path.join(opts.trash_dir, opts.stamp)
    dest = os.path.join(dest_dir, placement(real, root))
    if not is_within(os.path.realpath(dest), os.path.realpath(dest_dir)):
        refuse(root, "to-trash",
               "move would leave the destination directory, refusing: %s"
               % target)
    if os.path.lexists(dest):
        refuse(root, "to-trash",
               "destination already exists, refusing to overwrite: %s" % dest)

    say("    destination: %s" % dest)
    if not opts.apply:
        say("    would create the destination directory if absent")
        say("    would move (no delete)")
        return "move-dry-run", False

    try:
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        try:
            os.rename(target, dest)
            how = "rename"
        except OSError:
            shutil.move(target, dest)
            how = "copy+delete"
    except OSError as exc:
        say("    move failed: %s" % ascii_safe(str(exc)))
        return "move-failed", os.path.lexists(target)

    gone = not os.path.lexists(target)
    landed = os.path.lexists(dest)
    say("    moved by %s; source gone=%s destination present=%s"
        % (how, gone, landed))
    if gone and landed:
        return "moved(%s)" % how, False
    say("    filesystem: source STILL PRESENT or destination missing")
    return "move-incomplete", True


# --------------------------------------------------------------------------
# --audit-only: what the Recycle Bin says it took
# --------------------------------------------------------------------------

def filetime_to_iso(value):
    if value <= 0:
        return "unknown"
    try:
        when = datetime.datetime(1601, 1, 1) + datetime.timedelta(
            microseconds=value // 10)
    except OverflowError:
        return "out-of-range"
    return when.strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_record(path):
    """$I v2: [0:8] version, [8:16] size, [16:24] FILETIME (1601 epoch),
    [24:28] path char count (4-byte LE), [28:] path as UTF-16LE.  Anything
    whose version prefix is not v2 falls back to 260 UTF-16LE chars at [8:]."""
    with open(path, "rb") as handle:
        raw = handle.read()
    if len(raw) < 28:
        return None
    version = int.from_bytes(raw[0:8], "little")
    if version == 2:
        size = int.from_bytes(raw[8:16], "little")
        stamp = int.from_bytes(raw[16:24], "little")
        count = int.from_bytes(raw[24:28], "little")
        body = raw[28:28 + count * 2]
    else:
        # Best effort only: the v1 layout is fuzzy and pre-Vista records are
        # rare, so keep the leading printable run of the fixed-width field.
        size = 0
        stamp = 0
        body = raw[8:8 + V1_PATH_CHARS * 2]
    text = body.decode("utf-16-le", "replace").lstrip("\x00").split("\x00")[0]
    return {"version": version, "size": size, "filetime": stamp, "path": text}


def record_kind(entry_name, size):
    sibling = os.path.join(os.path.dirname(entry_name),
                           "$R" + os.path.basename(entry_name)[2:])
    if os.path.isdir(sibling):
        return "dir"
    if os.path.lexists(sibling):
        return "file"
    return "file?" if size else "dir/empty?"


def audit_recycle_bin(drives, targets):
    records = []
    wanted = set(key_of(real) for _display, real in targets)
    say("")
    say("Recycle Bin records (drive(s): %s)" % ", ".join(drives))
    for drive in drives:
        base = os.path.join(drive + os.sep, "$Recycle.Bin")
        say("  scanning %s" % base)
        if not os.path.isdir(base):
            say("    not present")
            continue
        try:
            sids = sorted(os.listdir(base))
        except OSError as exc:
            say("    cannot list: %s" % ascii_safe(str(exc)))
            continue
        for sid in sids:
            sid_dir = os.path.join(base, sid)
            if not os.path.isdir(sid_dir):
                continue
            try:
                names = sorted(n for n in os.listdir(sid_dir)
                               if n.startswith("$I"))
            except OSError as exc:
                say("    %s: cannot list (%s)" % (sid, ascii_safe(str(exc))))
                continue
            for name in names:
                full = os.path.join(sid_dir, name)
                try:
                    record = parse_record(full)
                except OSError as exc:
                    say("    %s: unreadable (%s)" % (name, ascii_safe(str(exc))))
                    continue
                if record is None:
                    continue
                record["kind"] = record_kind(full, record["size"])
                record["sid"] = sid
                record["entry"] = full
                record["match"] = key_of(record["path"]) in wanted
                records.append(record)

    if not records:
        say("  no $I records found")
        return []

    say("")
    say("  %d record(s):" % len(records))
    for record in records:
        say("    original: %s" % record["path"])
        if record["kind"] == "file":
            say("      recorded size: %d" % record["size"])
        say("      kind=%s version=%d deleted=%s"
            % (record["kind"], record["version"],
               filetime_to_iso(record["filetime"])))
        if record["match"]:
            say("      MATCH: this is one of the requested paths")
    return records


def audit_drives(raw_targets, opts):
    """Drives to scan: from the given paths, plus/none of --drive."""
    drives = []
    for item in raw_targets:
        drive = os.path.splitdrive(os.path.abspath(item))[0]
        if drive and drive not in drives:
            drives.append(drive)
    if opts.drive:
        drive = opts.drive.rstrip("\\/")
        if not DRIVE_RE.match(drive):
            die("--drive must look like C: (got %s)" % drive)
        if drive not in drives:
            drives.append(drive)
    if not drives:
        drives = ["C:"]
    return drives


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

class Parser(argparse.ArgumentParser):
    """argparse exits 2 on a usage error; this tool reserves 2 for 'a target
    survived', so usage errors exit 1 like every other refusal."""

    def error(self, message):
        self.print_usage(sys.stderr)
        sys.stderr.write("safe_delete: %s\n" % ascii_safe(message))
        raise SystemExit(1)


def main(argv=None):
    parser = Parser(
        description="Workspace-confined, dry-run-by-default deletion that "
                    "always goes through the Recycle Bin.",
        epilog="exit codes: 0 = success or dry run; 1 = refused or usage "
               "error; 2 = ran but a target survived")
    parser.add_argument("paths", nargs="*", metavar="PATH",
                        help="literal file/directory paths; wildcards are "
                             "refused and never expanded")
    parser.add_argument("--apply", action="store_true",
                        help="actually delete (or move with --to-trash); "
                             "without this the run is a dry run")
    parser.add_argument("--max", type=int, default=DEFAULT_MAX, metavar="N",
                        help="batch cap, default %d" % DEFAULT_MAX)
    parser.add_argument("--allow-outside", action="store_true",
                        help="permit targets outside the root")
    parser.add_argument("--to-trash", metavar="DIR",
                        help="move targets into DIR/<UTC timestamp>/ instead "
                             "of deleting them")
    parser.add_argument("--audit-only", action="store_true",
                        help="read Recycle Bin metadata; delete nothing")
    parser.add_argument("--drive", metavar="D:",
                        help="drive to scan in --audit-only when no paths are "
                             "given (default C:)")
    opts = parser.parse_args(argv)

    root = REPO
    if opts.max < 1:
        die("--max must be at least 1")
    if opts.audit_only and opts.to_trash:
        die("--audit-only and --to-trash are mutually exclusive")
    if opts.audit_only and opts.apply:
        die("--audit-only deletes nothing, so --apply makes no sense with it")
    if not opts.paths and not opts.audit_only:
        parser.print_help()
        say("")
        say("Nothing to do: name at least one literal path "
            "(dry run unless --apply is given).")
        append_log(root, "no-args", [])
        return 1

    mode = ("audit-only" if opts.audit_only else
            "to-trash" if opts.to_trash else "delete")

    # --to-trash DIR is a write destination, so it is confined like a target.
    if opts.to_trash:
        for bad in WILDCARDS:
            if bad in opts.to_trash:
                refuse(root, mode, "wildcard %r in --to-trash: %s"
                       % (bad, opts.to_trash))
        trash_dir = os.path.abspath(opts.to_trash)
        if not is_within(os.path.realpath(trash_dir), root) \
                and not opts.allow_outside:
            refuse(root, mode,
                   "--to-trash destination is outside the root -- refusing: "
                   "%s (pass --allow-outside to override)" % trash_dir)
    else:
        trash_dir = None
    opts.trash_dir = trash_dir
    opts.stamp = utc_stamp()

    say("safe_delete")
    say("  mode       : %s%s" % (mode,
                                 "" if (opts.apply or opts.audit_only)
                                 else " (DRY RUN -- nothing will change)"))
    say("  root       : %s" % root)
    say("  batch cap  : %d" % opts.max)
    if trash_dir:
        say("  trash dir  : %s" % trash_dir)
        say("  run stamp  : %s" % opts.stamp)

    if opts.audit_only:
        drives = audit_drives(opts.paths, opts)
        targets = []
        if opts.paths:
            targets = resolve_targets(opts.paths, root, opts.allow_outside,
                                      opts.max, audit=True, mode=mode)
            say("  targets (drive selection only, existence not required):")
            for display, real in targets:
                say("    %s" % display)
                if display != real:
                    say("      resolves to %s" % real)
        else:
            say("  no paths given: scanning --drive %s" % drives[0])
        records = audit_recycle_bin(drives, targets)
        append_log(root, mode, [("-", "audit records=%d" % len(records))])
        return 0

    targets = resolve_targets(opts.paths, root, opts.allow_outside, opts.max,
                              audit=False, mode=mode)

    say("  targets    : %d" % len(targets))
    for display, real in targets:
        tracked, ignored = git_facts(real)
        say("    %s" % display)
        if display != real:
            say("      resolves to %s" % real)
        say("      exists=yes tracked=%s ignored=%s" % (tracked, ignored))
        if tracked == "no" and ignored.startswith("yes"):
            say("      verdict: ignored and untracked (safely deletable by "
                "the project rule)")
    if not trash_dir:
        say("  delete path: %s" % DELETE_CALL_NOTE)

    say("")
    say("%s:" % ("moving" if trash_dir else "deleting"))
    outcomes, survivors = check_apply_mode(targets, root, opts)

    append_log(root, mode + ("+apply" if opts.apply else "+dry-run"), outcomes)
    say("")
    say("  log        : %s"
        % os.path.join(root, LOG_DIR, "tmp", LOG_NAME))
    if survivors:
        say("  result     : %d target(s) SURVIVED" % survivors)
        return 2
    if not opts.apply:
        say("  result     : DRY RUN -- nothing was deleted, moved or created")
        return 0
    if trash_dir:
        say("  result     : all %d target(s) moved into the trash directory"
            % len(targets))
        return 0
    say("  result     : all %d target(s) confirmed gone" % len(targets))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
