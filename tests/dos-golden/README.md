# DOS golden traces (Keen 4, 5, 6)

`k<E>-d<N>.trc.zst` are reference traces of the five built-in demos of each episode, produced by the
**byte-identical DOS rebuild** of the original game (the `KEENDUMP` build in [`oracle/`](../../oracle/README.md)).
`MANIFEST.sha256` has the sha256 of each *uncompressed* trace; `oracle-report.txt` is the report of
the run that produced the Keen 4 and Keen 5 set (2026-10-31/11-01).

- **Keen 4**: recorded with the v1.4 shareware data (`EGAGRAPH.CK4`, `GAMEMAPS.CK4`, `AUDIO.CK4`).
- **Keen 5 and Keen 6**: recorded from the commercial v1.4 data. The traces are derived per-tic state
  dumps (gamestate and object array), not game data, and are committed so they travel with the repo.

The trace format is the one Omnispeak writes with `/DUMPFILE`: fixed-size per-frame records, each
holding a `TimeCount` word, the whole gamestate block and the 100-entry object array
(see [`oracle/docs/TRACE_FORMAT.md`](../../oracle/docs/TRACE_FORMAT.md)).

## CI covers Keen 4 only

`.github/workflows/dos-oracle.yml` downloads the Keen 4 shareware and checks all five demos, with and
without interpolation. Keen 5 and 6 are commercial, so CI has no data for them and leaves them
unchecked. To check them locally (with your own data in `$KEEN_DATA`):

```sh
zstd -d -c tests/dos-golden/k5-d0.trc.zst > /tmp/g.trc
(cd "$RUN" && omnispeak /EPISODE 5 /PLAYDEMO 0 /DUMPFILE /tmp/f.trc /NOINTERP)   # $RUN = data files + data/keen5/*
python3 oracle/tools/tracecmp.py cmp -e 5 /tmp/g.trc /tmp/f.trc
```

`oracle/harness/oracle-run.sh` does this for every demo.

## Reading a trace

```sh
python3 oracle/tools/tracecmp.py info -e 4 k4-d0.trc      # frames, live objects, TimeCount slack (uncompressed input)
python3 oracle/tools/tracecmp.py cmp  -e 4 A.trc B.trc    # exit 0 = equal on all compared fields
```
