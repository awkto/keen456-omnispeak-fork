# oracle/: how we prove the fork still plays exactly like the DOS original

This directory is the DOS reference for the rest of the repository. It answers one question: after
all the smoothing, fixes and ports, does the Omnispeak fork still behave tic-for-tic like the real
DOS game in its `vanilla` profile?

1. **Byte-identical DOS baseline** (`dos/`). K1n9_Duk3's reconstructed Keen 4-6 source, rebuilt with
   Borland C++ 3.x headless in DOSBox so the result is byte-identical to the shipped `KEEN4E.EXE`,
   `KEEN5.EXE` and `KEEN6*.EXE` (`make -C oracle verify-k4` and so on).
2. **Instrumented build** (`KEENDUMP`). The same source with a trace writer: it plays a built-in demo
   and writes the whole gamestate plus the object array for every game tic
   ([`docs/TRACE_FORMAT.md`](docs/TRACE_FORMAT.md)).
3. **Golden traces.** One trace per built-in demo of Keen 4, 5 and 6, committed zstd-compressed in
   [`../tests/dos-golden/`](../tests/dos-golden/README.md). The fork writes the same format with
   `/DUMPFILE`, and `tools/tracecmp.py` / `tools/tracediff.py` compare the two.
4. **CI.** `.github/workflows/dos-oracle.yml` re-checks the Keen 4 (shareware) demos on every push.

Full pre-consolidation history of this directory (when it was its own repository,
`keen456-decomp`) is in the archived <https://github.com/awkto/keen456-decomp>.
The issue tracker is <https://gitlab.dnsif.ca/github/keen456-omnispeak-fork>.

## Layout

```
dos/       reconstructed DOS source: pristine import, must keep building byte-identical
harness/   dosrun.sh (headless DOSBox-X runner), oracle-run.sh (full pass), interp/ (reference scripts)
tools/     verify-dos, dosbuild, keendump, unlzexe, ripstatic, tracecmp, tracediff, demo, statemap,
           map2syms, keendata.{py,sh}, and their tests
docs/      TOOLCHAIN, TRACE_FORMAT, VERSIONS, SYMBOLS, HARNESS, DEMOS, statemap-k*.tsv, symbols/
vendor/    (not in git) LZEXE.EXE and MAKEOBJ.EXE, needed by the rebuild
Makefile   targets: verify[-k4|-k5|-k6], keendump-k<N>, symbols, oracle
```

## What you need

Nothing here runs without things that are not in the repository:

- **Borland C++ 3.0/3.1** unpacked at `~/bcc` (`BC30`, `BC20`); see [`docs/TOOLCHAIN.md`](docs/TOOLCHAIN.md).
- **The original game files** under `$KEEN_DATA` (default `~/keen-data-decomp`, `keen4/`, `keen5/`,
  `keen6/`: the EXE plus `EGAGRAPH`, `GAMEMAPS`, `AUDIO`). Keen 4 v1.4 shareware is free
  (`../tools/fetch-k4-shareware.sh`); Keen 5 and 6 are commercial. Policy: [`../docs/DATA.md`](../docs/DATA.md).
- `oracle/vendor/LZEXE.EXE` and `oracle/vendor/MAKEOBJ.EXE` (gitignored, not redistributable here).
- DOSBox (the headless runner defaults to the pinned DOSBox-X build; override with `DOSBOX_X`), `zstd`, Python 3.

Tools print `SKIP` and exit 0 when the data or toolchain is absent. Docs here write `make verify-k4` for `make -C oracle verify-k4`; some older text still shows `dos/`, `harness/`, `tools/` relative to `oracle/`.

## Rebuild and regenerate traces

Run these from the repo root. Heavy work belongs on a throwaway machine, not a dev laptop
(`tools/droplet.sh` creates and feeds a DigitalOcean droplet).

```sh
make -C oracle verify-k4          # rebuild Keen 4 byte-identical to the shipped EXE (PASS / FAIL)
make -C oracle keendump-k4        # build the trace-writing variant
oracle/harness/oracle-run.sh      # full pass: rebuild, play every demo twice (must be byte-identical),
                                  # write traces to tests/dos-golden/, build the fork and compare
python3 oracle/tools/tracecmp.py cmp -e 4 golden.trc fork.trc      # exit 0 = equal on compared fields
python3 oracle/tools/tracediff.py -e 4 golden.trc fork.trc         # first divergence, with state names
```

`oracle-run.sh` writes uncompressed `k<E>-d<N>.trc` files; compress with `zstd` before committing
(`*.trc` is gitignored, only `*.trc.zst` is allowed by `tools/check-no-data.sh`).
The Keen 5 and Keen 6 traces were produced locally from the commercial data, then committed as
derived per-tic state dumps; they contain no game assets.

## Tests

```sh
python3 -m unittest discover -s oracle/tools -p 'test_*.py'     # no game data needed
```

See [`NOTICE`](NOTICE) for the licences of the reconstructed source and the projects it builds on.
