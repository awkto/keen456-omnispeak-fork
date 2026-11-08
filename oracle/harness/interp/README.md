# Interpolation acceptance scripts (#92)

Kept as a **reference**, not as ready-to-run tools: they contain the session scratch paths they were run from.
- `offruns.sh`: runs built-in demos on an unpatched upstream build (`base`) and on our fork (`feat`) with Omnispeak's `/DUMPFILE` (CK_DEBUG builds) and compares the dumps.
- `cam.gdb`: gdb script that logs the camera origin per frame.
- `stats.py`: scroll-step standard deviation from an `OMNI_DEBUG_INTERP_LOG` CSV.

These become proper tools in #82 (motion trace) and #60/#62 (sync checks).
