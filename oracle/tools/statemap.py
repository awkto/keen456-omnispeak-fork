#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Map DOS actor states (statetype) to Omnispeak actions (CK_action), Keen 4 / Keen 5.

    oracle/tools/statemap.py <4|5> [--dos DIR] [--action FILE] [-o OUT.tsv]

Reads only source text: the `statetype x = {...};` initialisers in oracle/dos/KEEN4-6 (with the
episode's #ifdefs applied, EGA build) and the `%action` lines of Omnispeak's ACTION.CK<n>
(default: the repo's data/ tables). No game data and no executable is read.

Why it cannot match by name: DOS calls a state `s_keenstand`, Omnispeak `CK_ACT_keenStanding`.
The names were chosen independently and almost never agree. The real join key is the DOS
pointer: `objtype.state` is a near (16-bit, DS-relative) pointer in the medium-model DOS
build, and every Omnispeak action records that value as `compatDosPointer` (2nd field of
`%action`). The source cannot give us DS offsets (that needs the linker), so this script
recovers the correspondence structurally:

  1. DOS states are listed in link order (CK_STATE, CK_KEEN, CK_KEEN2, K?_SPEC, K?_ACT1..3,
     from RCK?.PRJ) and declaration order: that is the order the compiler lays them out
     in _DATA, i.e. ascending DS offset.
  2. Omnispeak actions are sorted by compatDosPointer.
  3. The two sequences are aligned (difflib) on a signature of the fields both sides
     share verbatim: progress/type, skippable/protectAnimation, pushtofloor/stickToGround,
     tictime/timer, xmove/velX, ymove/velY, left/right shape name, and which of
     think/contact/react/next are NULL.
  4. Each pair is cross-checked: the DOS nextstate must map to the Omnispeak next action.

Output: TSV, one row per state, in DS order. `status` is
  match       signature equal and next-state consistent
  match~      paired by position inside a differing run; see `diff`
  dos-only    DOS state with no Omnispeak action
  omni-only   Omnispeak action with no DOS state
"""
import argparse, difflib, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))
DOSDIR = os.path.join(REPO, "dos", "KEEN4-6")

# State-bearing modules in link order (RCK4.PRJ / RCK5.PRJ; CK_MAIN/DEMO/GAME/PLAY/TEXT
# and the ID_* modules declare no statetype).
LINK = {
    4: ["CK_STATE.C", "CK_KEEN.C", "CK_KEEN2.C", "KEEN4/K4_SPEC.C",
        "KEEN4/K4_ACT1.C", "KEEN4/K4_ACT2.C", "KEEN4/K4_ACT3.C"],
    5: ["CK_STATE.C", "CK_KEEN.C", "CK_KEEN2.C", "KEEN5/K5_SPEC.C",
        "KEEN5/K5_ACT1.C", "KEEN5/K5_ACT2.C", "KEEN5/K5_ACT3.C"],
}
PROGRESS = ["step", "slide", "think", "stepthink", "slidethink"]
# Omnispeak CK_ActionType has the same numeric values (src/ck_def.h CK_ActionType)
OMNITYPE = ["UnscaledOnce", "ScaledOnce", "Frame", "UnscaledFrame", "ScaledFrame"]
CONSTS = {"false": 0, "true": 1, "NULL": 0, "PIXGLOBAL": 16, "TILEGLOBAL": 256}


# ---------------------------------------------------------------- DOS side

def strip_comments(text):
    """Remove /* */ and // comments, keeping newlines so line numbers survive."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def pp_eval(expr, defines):
    expr = re.sub(r"\bdefined\s*\(?\s*(\w+)\s*\)?",
                  lambda m: " 1 " if m.group(1) in defines else " 0 ", expr)
    expr = expr.replace("&&", " and ").replace("||", " or ")
    expr = re.sub(r"!(?!=)", " not ", expr)
    expr = re.sub(r"\b[A-Za-z_]\w*\b",
                  lambda m: m.group(0) if m.group(0) in ("and", "or", "not")
                  else str(defines.get(m.group(0), 0)), expr)
    return bool(eval(expr, {"__builtins__": {}}))


def preprocess(text, defines):
    """Blank out lines in inactive #if branches. Handles #if/#ifdef/#ifndef/#elif/#else/#endif."""
    out, stack = [], []  # stack entries: [parent_active, this_branch_active, any_taken]
    active = True
    for line in text.split("\n"):
        m = re.match(r"\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)", line)
        if m:
            kw, arg = m.group(1), m.group(2).strip()
            if kw in ("ifdef", "ifndef", "if"):
                if kw == "ifdef":
                    cond = arg.split()[0] in defines
                elif kw == "ifndef":
                    cond = arg.split()[0] not in defines
                else:
                    cond = pp_eval(arg, defines)
                stack.append([active, active and cond, cond])
            elif kw == "elif":
                parent, _, taken = stack[-1]
                cond = (not taken) and pp_eval(arg, defines)
                stack[-1] = [parent, parent and cond, taken or cond]
            elif kw == "else":
                parent, _, taken = stack[-1]
                stack[-1] = [parent, parent and not taken, True]
            else:
                stack.pop()
            active = stack[-1][1] if stack else True
            out.append("")
        else:
            out.append(line if active else "")
    return "\n".join(out)


def num(tok):
    tok = tok.strip()
    try:
        return int(eval(tok, {"__builtins__": {}}, CONSTS))
    except Exception:
        return tok


def dos_states(ep, dosdir):
    defines = {f"KEEN{ep}": 1, "GRMODE": 1, "EGAGR": 1, "CGAGR": 2}
    states = []
    for rel in LINK[ep]:
        raw = open(os.path.join(dosdir, rel), encoding="latin-1").read()
        text = preprocess(strip_comments(raw), defines)
        for m in re.finditer(r"\bstatetype\s+(\w+)\s*=\s*\{([^}]*)\}\s*;", text, re.S):
            f = [x.strip() for x in m.group(2).split(",")]
            if len(f) != 12:
                sys.exit(f"{rel}: cannot parse {m.group(1)}: {f}")
            nxt = f[11].lstrip("&").strip()
            states.append({
                "name": m.group(1),
                "src": f"dos/KEEN4-6/{rel}:{text.count(chr(10), 0, m.start()) + 1}",
                "left": f[0], "right": f[1],
                "progress": PROGRESS.index(f[2]),
                "skip": num(f[3]), "push": num(f[4]), "tictime": num(f[5]),
                "xmove": num(f[6]), "ymove": num(f[7]),
                "think": f[8].lstrip("&"), "contact": f[9].lstrip("&"), "react": f[10].lstrip("&"),
                "next": None if nxt in ("NULL", "0") else nxt,
            })
    return states


# ----------------------------------------------------------- Omnispeak side

def omni_actions(path):
    acts, aliases = [], []
    for ln, line in enumerate(open(path, encoding="latin-1"), 1):
        line = line.split("#")[0].strip()
        if not line.startswith("%action"):
            continue
        t = line.split()
        if len(t) >= 4 and t[2] == "=":
            aliases.append((t[1], t[3]))
            continue
        if len(t) != 15:
            sys.exit(f"{path}:{ln}: unexpected %action arity {len(t)}")
        nxt = t[14]
        acts.append({
            "name": t[1], "offset": int(t[2], 0), "line": ln,
            "left": t[3], "right": t[4],
            "progress": OMNITYPE.index(t[5]) if t[5] in OMNITYPE else int(t[5], 0),
            "skip": int(t[6], 0), "push": int(t[7], 0), "tictime": int(t[8], 0),
            "xmove": int(t[9], 0), "ymove": int(t[10], 0),
            "think": t[11], "contact": t[12], "react": t[13],
            "next": None if nxt == "NULL" else nxt,
        })
    acts.sort(key=lambda a: a["offset"])
    return acts, aliases


# ---------------------------------------------------------------- matching

def shape(s):
    """DOS 'KEENSTANDLSPR' and Omnispeak '@SPR_KEENSTANDL' -> 'KEENSTANDL'; numbers stay numbers."""
    s = s.strip()
    if s.startswith("@SPR_"):
        return s[5:]
    if s.endswith("SPR") and not s[0].isdigit():
        return s[:-3]
    v = num(s)
    return v if isinstance(v, int) else s


def isnull(f):
    return f in (None, "NULL", "0", "")


FIELDS = ["progress", "skip", "push", "tictime", "xmove", "ymove"]


def sig(s):
    return tuple([s[k] for k in FIELDS] + [shape_kind(s["left"]), shape_kind(s["right"])]
                 + [isnull(s[k]) for k in ("think", "contact", "react", "next")])


def shape_kind(s):
    """Sprite names differ between the two projects (DOS GFXE_CK?.H vs Omnispeak's own
    @SPR_ names, occasionally a bare chunk number), so the alignment only uses whether a
    shape is a sprite or one of the special values 0 (none) / -1 (invisible)."""
    v = shape(s)
    return v if isinstance(v, int) and v in (0, -1) else "SPR"


def diff(d, o):
    out = [f"{k}:{d[k]}/{o[k]}" for k in FIELDS if d[k] != o[k]]
    for k in ("left", "right"):
        if shape_kind(d[k]) != shape_kind(o[k]):
            out.append(f"{k}:{d[k]}/{o[k]}")
    for k in ("think", "contact", "react", "next"):
        if isnull(d[k]) != isnull(o[k]):
            out.append(f"{k}-null:{isnull(d[k])}/{isnull(o[k])}")
    return ",".join(out)


def spr(d, o):
    """'same' if both sprite names agree after normalisation, else 'DOSNAME=OMNINAME' pairs."""
    if not (d and o):
        return ""
    pairs = []
    for k in ("left", "right"):
        a, b = shape(d[k]), shape(o[k])
        if a != b and f"{a}={b}" not in pairs:
            pairs.append(f"{a}={b}")
    return "same" if not pairs else " ".join(pairs)


def align(ds, os_):
    sm = difflib.SequenceMatcher(None, [sig(s) for s in ds], [sig(a) for a in os_], autojunk=False)
    rows = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            rows += [("match", ds[i], os_[j]) for i, j in zip(range(i1, i2), range(j1, j2))]
        elif tag == "replace" and (i2 - i1) == (j2 - j1):
            rows += [("match~", ds[i], os_[j]) for i, j in zip(range(i1, i2), range(j1, j2))]
        else:
            rows += [("dos-only", ds[i], None) for i in range(i1, i2)]
            rows += [("omni-only", None, os_[j]) for j in range(j1, j2)]
    # next-state cross-check
    d2o = {d["name"]: o["name"] for st, d, o in rows if d and o}
    out = []
    for st, d, o in rows:
        note = diff(d, o) if d and o else ""
        nextok = ""
        if d and o:
            want = d2o.get(d["next"]) if d["next"] else None
            nextok = "yes" if want == o["next"] else "NO"
            if nextok == "NO" and st == "match":
                st = "match~"
                note = (note + "," if note else "") + "next-target"
        out.append((st, d, o, nextok, note))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("episode", type=int, choices=(4, 5))
    ap.add_argument("--dos", default=DOSDIR)
    ap.add_argument("--action")
    ap.add_argument("-o", "--out")
    a = ap.parse_args()
    action = a.action or os.path.join(REPO, "..", "data", f"keen{a.episode}", f"ACTION.CK{a.episode}")
    if not os.path.exists(action):
        print(f"SKIP: {action} not found (git submodule update --init?)")
        return 0
    ds = dos_states(a.episode, a.dos)
    acts, aliases = omni_actions(action)
    rows = align(ds, acts)

    count = {}
    for r in rows:
        count[r[0]] = count.get(r[0], 0) + 1
    same_name = sum(1 for _, d, o, _, _ in rows if d and o and d["name"] == o["name"])
    nextbad = sum(1 for r in rows if r[3] == "NO")
    # Layout check: inside one DOS module, consecutive states are 30 bytes apart
    # (sizeof(statetype) in the medium model) unless other initialised data sits between them.
    gaps, desc, prev = 0, 0, None
    for _, d, o, _, _ in rows:
        if d and o:
            mod = d["src"].rsplit(":", 1)[0]
            if prev and prev[0] == mod:
                gaps += (o["offset"] - prev[1]) != 30
                desc += o["offset"] < prev[1]
            prev = (mod, o["offset"])
    sprdiff = sum(1 for _, d, o, _, _ in rows if d and o and spr(d, o) != "same")
    lines = [
        f"# statemap Keen {a.episode}: generated by oracle/tools/statemap.py from source text only; do not edit",
        f"# DOS states: {len(ds)}  Omnispeak actions: {len(acts)} (+{len(aliases)} alias lines)",
        "# " + "  ".join(f"{k}: {count.get(k, 0)}" for k in ("match", "match~", "dos-only", "omni-only"))
        + f"  identical-names: {same_name}  next-mismatch: {nextbad}  sprite-names-differ: {sprdiff}",
        f"# DS layout: {desc} out-of-order offsets, {gaps} within-module gaps != 30 bytes (interleaved data)",
        "# omni_dsofs = Omnispeak compatDosPointer = DOS near pointer value of the state (DS offset)",
        "\t".join(["status", "dos_name", "dos_src", "omni_name", "omni_dsofs", "omni_line",
                   "progress", "tictime", "dos_next", "omni_next", "next_ok", "sprite_names", "diff"]),
    ]
    for st, d, o, nextok, note in rows:
        ref = d or o
        lines.append("\t".join([
            st,
            d["name"] if d else "", d["src"] if d else "",
            o["name"] if o else "", f"0x{o['offset']:04X}" if o else "",
            str(o["line"]) if o else "",
            PROGRESS[ref["progress"]] if isinstance(ref["progress"], int) and ref["progress"] < 5 else str(ref["progress"]),
            str(ref["tictime"]),
            (d["next"] or "NULL") if d else "", (o["next"] or "NULL") if o else "",
            nextok, spr(d, o), note,
        ]))
    for src, dst in aliases:
        lines.append("\t".join(["omni-alias", "", "", src, "", "", "", "", "", dst, "", "", f"alias of {dst}"]))
    text = "\n".join(lines) + "\n"
    if a.out:
        open(a.out, "w").write(text)
    else:
        sys.stdout.write(text)
    print(lines[1][2:] + " | " + lines[2][2:], file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
