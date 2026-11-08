#!/bin/bash
S=/tmp/claude-1000/-home-altanc-sessions-keen-decomp/b0ec9262-a891-4041-97aa-daf03d265e09/scratchpad
cd $S/run; export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
for b in base feat; do for ep in 4 5; do for d in 0 1; do
  [ $b = base ] && [ $ep = 4 ] && [ $d = 0 ] && continue
  timeout 180 nice -n 19 ionice -c3 gdb -q -batch -x $S/cam.gdb --args $S/$b-sdl2 /EPISODE $ep /PLAYDEMO $d /DUMPFILE $S/${b}_k${ep}_d$d.dump > $S/${b}_k${ep}_d$d.gdb 2>&1
  echo "$b k$ep d$d rc=$? frames=$(grep -c ^CAM $S/${b}_k${ep}_d$d.gdb)"
done; done; done
