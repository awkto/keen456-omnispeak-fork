# DOS golden traces (Keen 4)

`k4-d0.trc.zst` … `k4-d4.trc.zst` are reference traces of the five built-in Keen 4
demos, produced by the **byte-identical DOS rebuild** of the original game — the
`keen456-decomp` project's `KEENDUMP` build — running the Keen 4 **v1.4 shareware**
data (`EGAGRAPH.CK4`, `GAMEMAPS.CK4`, `AUDIO.CK4`).

The trace format is the same one Omnispeak writes with `/DUMPFILE`: a sequence of
fixed-size per-frame records, each holding a `TimeCount` word, the whole gamestate
block, and the 100-entry object array (see `docs/TRACE_FORMAT.md`). Because the DOS
and Omnispeak builds are byte-identical in the parts that matter, the traces can be
compared field-for-field to prove the engine still reproduces the original game.

`tracecmp.py` is a small reader/comparer:

```sh
python3 tracecmp.py info -e 4 k4-d0.trc.zst    # frames, live objects, TimeCount slack
python3 tracecmp.py cmp  -e 4 A.trc B.trc      # exit 0 = equal on all compared fields
```

It compares the gamestate block and, for every live object (the list from slot 0
along `next`), every field except `sprite`; `next`/`prev` are compared as slot
numbers. `TimeCount` (wall-clock), free slots and `sprite` are ignored.

## Running the same check locally

Build a debug (playloop-dumper) SDL2 build, then run each demo in a directory that
contains both the shareware data and the Omnispeak data files:

```sh
make -C src -j4 DEBUG=1 RENDERER=sdl2        # binary: bin/omnispeak

# get the shareware data once
mkdir -p /tmp/keen4 && cd /tmp/keen4
wget https://davidgow.net/keen/4keen14.zip && unzip -o 4keen14.zip
for f in EGAGRAPH GAMEMAPS AUDIO; do
  cp "$(find . -maxdepth 1 -iname "${f}.CK4" | head -n1)" "./${f}.CK4"
done
cp -r /path/to/repo/data/keen4/* .

# for each demo 0..4 and each mode
zstd -d -c /path/to/repo/tests/dos-golden/k4-d0.trc.zst > golden.trc
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  /path/to/repo/bin/omnispeak /EPISODE 4 /PLAYDEMO 0 /DUMPFILE out.trc /NOINTERP
python3 /path/to/repo/tests/dos-golden/tracecmp.py cmp -e 4 golden.trc out.trc
```

Run the same command with `/INTERP pixel` in place of `/NOINTERP` to check the
interpolating presenter (SDL2 renderers only). `.github/workflows/dos-oracle.yml`
does exactly this for all five demos in both modes on every push/PR to `main`.
