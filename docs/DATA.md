# Game data: policy, layout, versions

## Policy
- **Game data is never committed**, never put in CI artifacts or logs, and never pushed anywhere except
  the temporary `claude-test` droplets (`tools/droplet.sh`). `.gitignore` blocks `*.CK?`, `*.EXE` etc.
- Every tool reads data from **`$KEEN_DATA`, default `~/keen-data-decomp`**. Nothing else defaults to
  another place. **Never default to, or write to, `~/keen-data`**: it is mounted into the live keen456
  containers (and its Keen 4/6 are older builds anyway).
- When data is absent a tool prints `SKIP: <reason>` and **exits 0**, so `make` targets and CI stay green
  without data. A tool that has the data but finds an unsupported version fails loudly instead.

## Layout
```
$KEEN_DATA/
  keen4/  KEEN4E.EXE  EGAGRAPH.CK4  GAMEMAPS.CK4  AUDIO.CK4   (v1.4 shareware)
  keen5/  KEEN5.EXE   EGAGRAPH.CK5  GAMEMAPS.CK5  AUDIO.CK5   (v1.4 Apogee)
  keen6/  KEEN6.EXE   EGAGRAPH.CK6  GAMEMAPS.CK6  AUDIO.CK6   (empty until #90)
```
File names are matched case-insensitively. The episode dirs may be symlinks. Extra files (saves,
`CONFIG.CK?`, `.conf`) are ignored by the tools.

## Versions
Only v1.4 EGA builds are supported (details and sha256 prefixes: [VERSIONS.md](VERSIONS.md)):
Keen 4 v1.4 (`KEEN4E.EXE`, packed 105,108 bytes), Keen 5 v1.4. Keen 6 needs v1.4/v1.5 (FormGen).
The identification is `ripstatic.fits()` against `oracle/dos/KEEN4-6/static/ripck?.pat`; from python call
`keendata.identify(ep)` (returns `'1.4'` or `None`), or run `python3 oracle/tools/keendata.py` to print a
status line per episode.

**Keen 4 shareware** is freely redistributable. Sources: the `games/keen4.jsdos` bundle in the keen456
repo (unzip it, take `KEEN4E.EXE` + `*.CK4`), or any v1.4 Apogee shareware archive (`4keen14.zip`
from the usual shareware mirrors). Keen 5 and 6 are commercial: supply your own copies.

## Resolver API
- Python: `oracle/tools/keendata.py`: `data_root()`, `episode_dir(ep)`, `find_file(dir, names)`, `exe(ep)`,
  `missing(ep)`, `require(ep)` (episode dir or SKIP+exit 0), `skip(msg)`, `skip_if_missing_file(path)`,
  `identify(ep)`.
- Shell (sourced): `oracle/tools/keendata.sh`: `$KD`, `kd_dir N`, `kd_exe N`, `kd_require N [noexe]`,
  `kd_find dir name`, `kd_stage N dir`.
- New tools must use these instead of hard-coding paths.

## What the engine (this repo's Omnispeak fork, `src/` + `data/`) needs
Two groups of files; Omnispeak looks for the first in its *Keen path* (cwd, or `/GAMEPATH <dir>`) and
the second in its *Omnispeak path* (the binary's own directory; with `FS_PREFER_KEEN_PATH` the Keen
path is tried first).

| group | files per episode | from |
|---|---|---|
| game data | `EGAGRAPH.CK?`, `GAMEMAPS.CK?`, `AUDIO.CK?` (no EXE needed) | `$KEEN_DATA/keen?/` |
| Omnispeak tables | `ACTION`, `AUDINFOE`, `AUDIODCT`, `AUDIOHHD`, `EGADICT`, `EGAHEAD`, `EPISODE`, `GFXCHUNK`, `GFXINFOE`, `MAPHEAD`, `STRINGS`, `TILEINFO` (`.CK?`) | `data/keen4`, `keen5`, `keen6e14` or `keen6e15` (in git) |

The tables are hand-written for the v1.4 data files; they are why Omnispeak rejects the older Keen 4
build. Run with a separate `/USERPATH` so saves and config never land in `$KEEN_DATA`.
Harness scripts stage a run dir with `. oracle/tools/keendata.sh; kd_stage 4 "$RUN"` (copies the three data files
plus the tables) and then run `omnispeak /GAMEPATH "$RUN" /EPISODE 4 /USERPATH "$RUN/user"`.

## CI helpers
- `tools/fetch-k4-shareware.sh` — download the freely redistributable Keen 4 v1.4 shareware (`$K4_URL`) and
  extract `KEEN4E.EXE` + `*.CK4` into `$KEEN_DATA/keen4` for CI.
- `tools/check-no-data.sh` — fail if any `*.CK?`, `*.EXE`, `*.COM` or `*.trc` file is tracked (only `tests/dos-golden/*.trc.zst`, derived per-tic state dumps, are allowed) or staged.

Enable the guard as a pre-commit hook:
```
ln -s ../../tools/check-no-data.sh .git/hooks/pre-commit
```
