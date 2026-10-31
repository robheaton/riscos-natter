#!/usr/bin/env python3
"""rozip.py: makes zips that RISC OS unpacks with the right file types.

A zip made on another system marks a RISC OS file type with a ",xxx" suffix in the name (!RunImage,ff8): that works when files are copied across a share, but a RISC OS
unzip (SparkFS, Info-ZIP's, the one that the Filer uses) does not read it: the files come out as Data and with ",ff8" in the name.  The RISC OS way is the "AC" extra field
(Info-ZIP's ARC0: load and exec addresses that hold the file type and the date, and the attributes) on every entry, with plain names.  This script writes that.

  rozip.py IN.zip OUT.zip [--replace NAME=FILE ...]      rewrite a zip: the suffix goes off the name and its hex digits become the file type; NAME (as it is after that) can be
                                                          replaced by the contents of FILE (its type and date stay)
  rozip.py --in-place ZIP [--replace NAME=FILE ...]      the same, on the file itself
  rozip.py --from-dir DIR OUT.zip [--top NAME]           a zip of the files under DIR (names with ,xxx suffixes as on the disc), in a folder NAME if given

A name without a suffix gets Text (&FFF) for README, LICENSE, NOTICE, COPYING and the usual text extensions, else Data (&FFD).  An entry that already has the ARC0 field is kept as it is.
The data, the order and the dates of the entries are kept; names that have a "." in them are left alone (RISC OS turns the "." into "/", as it does for any zip)."""
import calendar, os, re, struct, sys, time, zipfile

SUFFIX = re.compile(r",([0-9a-fA-F]{3})$")
TEXT_NAMES = {"README", "LICENSE", "LICENCE", "NOTICE", "COPYING", "ca-bundle"}
TEXT_EXT = {".md", ".txt", ".py", ".sh", ".c", ".h", ".cfg", ".toml", ".patch", ".json", ".csv"}

def default_type(name):
    leaf = name.rsplit("/", 1)[-1]
    if leaf in TEXT_NAMES or os.path.splitext(leaf)[1].lower() in TEXT_EXT:
        return 0xFFF
    return 0xFFD

def riscos_extra(ftype, unix, attr=0x13):
    """the ARC0 field (RISC OS load/exec: file type and date, and the attributes) and the Unix time field"""
    cs = int((unix + 2208988800) * 100)               # centiseconds since 1900
    hi, lo = (cs >> 32) & 0xFF, cs & 0xFFFFFFFF
    load = 0xFFF00000 | (ftype << 8) | hi
    ac = b"ARC0" + struct.pack("<III", load, lo, attr) + b"\0\0\0\0"
    return struct.pack("<HH", 0x4341, len(ac)) + ac + struct.pack("<HHBI", 0x5455, 5, 1, int(unix))

def has_arc0(extra):
    i = 0
    while i + 4 <= len(extra):
        hid, sz = struct.unpack("<HH", extra[i:i + 4])
        if hid == 0x4341 and extra[i + 4:i + 8] == b"ARC0":
            return True
        i += 4 + sz
    return False

def split_name(name):
    m = SUFFIX.search(name)
    return (name[:m.start()], int(m.group(1), 16)) if m else (name, default_type(name))

def _entry(name, ftype, date_time, unix, ext_attr):
    zi = zipfile.ZipInfo(name, date_time)
    zi.compress_type = zipfile.ZIP_DEFLATED
    zi.external_attr = ext_attr
    zi.extra = riscos_extra(ftype, unix)
    return zi

def convert(src_path, dst_path, replace=None):
    replace = replace or {}
    src = zipfile.ZipFile(src_path)
    n, used = 0, set()
    with zipfile.ZipFile(dst_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for zi in src.infolist():
            if zi.filename.endswith("/"):
                z.writestr(zi.filename, b"")
                continue
            data = src.read(zi.filename)
            if has_arc0(zi.extra) and not SUFFIX.search(zi.filename):
                name, ni = zi.filename, zi                          # already a RISC OS zip entry: keep it
                if name in replace:
                    data = open(replace[name], "rb").read(); used.add(name)
                z.writestr(ni, data)
                n += 1
                continue
            name, ftype = split_name(zi.filename)
            if name in replace:
                data = open(replace[name], "rb").read(); used.add(name)
            unix = calendar.timegm(zi.date_time + (0, 0, 0))
            z.writestr(_entry(name, ftype, zi.date_time, unix, zi.external_attr), data)
            n += 1
    missing = set(replace) - used
    if missing:
        raise SystemExit("--replace: no such entry: " + ", ".join(sorted(missing)))
    return n

def from_dir(root, dst_path, top=None):
    n = 0
    with zipfile.ZipFile(dst_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dp, dn, fn in os.walk(root):
            dn.sort()
            for f in sorted(fn):
                p = os.path.join(dp, f)
                rel = os.path.relpath(p, root).replace(os.sep, "/")
                name, ftype = split_name((top + "/" if top else "") + rel)
                st = os.stat(p)
                dt = time.localtime(st.st_mtime)[:6]
                z.writestr(_entry(name, ftype, dt, st.st_mtime, 0o100644 << 16), open(p, "rb").read())
                n += 1
    return n

def main(argv):
    replace, a, i = {}, [], 0
    top = None
    flags = set()
    while i < len(argv):
        x = argv[i]
        if x == "--replace":
            k, v = argv[i + 1].split("=", 1); replace[k] = v; i += 2
        elif x == "--top":
            top = argv[i + 1]; i += 2
        elif x in ("--in-place", "--from-dir"):
            flags.add(x); i += 1
        else:
            a.append(x); i += 1
    if "--from-dir" in flags and len(a) == 2:
        print("%d files" % from_dir(a[0], a[1], top)); return
    if "--in-place" in flags and len(a) == 1:
        tmp = a[0] + ".tmp"
        n = convert(a[0], tmp, replace); os.replace(tmp, a[0]); print("%d files converted in %s" % (n, a[0])); return
    if len(a) == 2 and not flags:
        print("%d files converted" % convert(a[0], a[1], replace)); return
    sys.exit(__doc__)

if __name__ == "__main__":
    main(sys.argv[1:])
