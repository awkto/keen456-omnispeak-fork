# Built-in demos

The attract-mode demos are ordinary `EGAGRAPH` chunks. `oracle/tools/demo.py` lists, dumps, verifies
and builds them (data from `$KEEN_DATA`, default `~/keen-data-decomp`; prints `SKIP` if absent).
Sources: `oracle/dos/KEEN4-6/ID_IN.C` (`IN_ReadControl`, recorder and playback), `CK_DEMO.C`
(`RunDemo`), `ID_CA.C` (`CAL_ExpandGrChunk`, `CAL_HuffExpand`).

## Where the chunks are

`DEMO0..DEMO4` are the last five externs in `GFXE_CK?.H`: chunk 4746..4750 in Keen 4,
4926..4930 in Keen 5 (Keen 6: 5555.., untested, no data). `EGAHEAD` (3-byte little-endian file
offsets, `0xFFFFFF` = sparse) and `EGADICT` (Huffman dictionary) are linked into the EXE, not
shipped as files; the tool unpacks the LZEXE EXE and reads them at the offsets in
`oracle/dos/KEEN4-6/static/ripck?.pat` (same code as `oracle/tools/ripstatic.py`).

## Chunk format

Compressed chunk = `u32 expanded_length` + Huffman stream (dictionary: 255 nodes of two
LE words, leaf if `< 256`, else node `v-256`; head node 254; bits LSB-first).

Expanded demo (this is what `RunDemo` reads):

| offset | size | meaning |
|---|---|---|
| 0 | u16 | `gamestate.mapon`, the level to load (map number in GAMEMAPS) |
| 2 | u16 | `DemoSize`: byte length of the RLE data that follows |
| 4 | DemoSize | pairs `(count, ctrl)`; `count` frames of control byte `ctrl` |
| 4+DemoSize | rest | trailer, never read (see below) |

Control byte (`IN_ReadControl`): bits 0-1 = `my+1` (0 up, 1 none, 2 down), bits 2-3 = `mx+1`
(0 left, 1 none, 2 right), bit 4 = button0 (jump), bit 5 = button1 (pogo/fire). Neutral = `0x05`.
One pair entry is consumed per `IN_ReadControl` call, i.e. one **frame** = one game-loop
iteration of `DEMOTICS = 3` tics (70 Hz tics, so ~23 frames/s in playback). The recorder caps
runs at 255 and starts a new pair on any change, so encoding is canonical: frames -> bytes is
unique and `oracle/tools/demo.py` reproduces it exactly. Playback ending early means
`Quit("Demo playback exceeded")`.

Trailer: four shipped demos (Keen 4 DEMO1, Keen 5 DEMO0, DEMO1) have extra bytes after
`DemoSize` (stale recording-buffer contents). They are ignored by the game but kept
(`"trailer"` hex in the JSON) so re-encoding is byte-identical. Also, when the Huffman stream
ends exactly on a byte boundary (Keen 5 DEMO1) the original packer appended one zero byte;
`verify` accepts exactly that case.

## JSON

`dump` writes `{"episode","demo","level","frame_count","tics_per_frame","trailer","frames":[{"x":-1..1,"y":-1..1,"b0":0|1,"b1":0|1}, ...]}`
(one frame per line). `build` reads that, and also a run form for hand-written scripts:
`{"level": 1, "runs": [{"n": 30, "x": 1}, {"n": 5, "x": 1, "b0": 1}]}` (omitted fields = 0;
`frames` entries come first, then `runs`). `build -o out.bin` writes the expanded demo;
`--chunk -e N` Huffman-compresses it into an EGAGRAPH chunk with the episode's dictionary.

## Table (Keen 4 and 5, v1.4 shareware data)

Level name from the map header in GAMEMAPS. "HighScores" is the demo that plays on the
high-score screen (`scorescreenkludge`, `RunDemo(4)`).

| episode | demo # | level | frames | level name |
|---|---|---|---|---|
| 4 | 0 | 1 | 812 | Border Village |
| 4 | 1 | 9 | 687 | Miragia |
| 4 | 2 | 11 | 749 | Pyramid of Moons |
| 4 | 3 | 15 | 645 | Isle of Tar |
| 4 | 4 | 19 | 271 | HighScores |
| 5 | 0 | 1 | 842 | Ion Vent System |
| 5 | 1 | 3 | 1024 | Def Tunnel Vlook |
| 5 | 2 | 10 | 1381 | Brownian Motion |
| 5 | 3 | 11 | 1267 | Grav Damping Hub |
| 5 | 4 | 15 | 703 | HighScores |

(Names are as stored in the map headers, which omit some spaces.) `oracle/tools/demo.py verify`: all 10
demos decode and re-encode byte-identically, both the demo bytes and the Huffman chunk.
`oracle/tools/test_demo.py` covers the codec with synthetic data only.
