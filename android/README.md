# Android build (#102)

A native Android APK of the engine: Omnispeak (this repo's `src/`) cross-compiled with the
NDK into `libmain.so`, running on SDL2's own Java glue under a small Kotlin app. It is the
native SDL2 renderer, not the WASM build in a WebView, so smoothing (frame interpolation)
runs at the display's refresh rate exactly as on desktop.

The shell follows the DOSBox-X app in the keen456 repo (`android-native/`): episode picker,
the same touch layout and palette. applicationId `com.awkto.keen456.omni`, so it installs
beside the WASM app (`com.awkto.keen456`) and the DOSBox-X app (`com.awkto.keen456.dx`).

## Build

    ANDROID_HOME=/path/to/sdk tools/build-android.sh     # -> dist/keen456-omni-<sha>-debug.apk

Needs JDK 17 and an Android SDK with `platforms;android-34`, `build-tools;34.0.0`,
`ndk;27.2.12479018` and `cmake;3.22.1`. The script:

1. fetches SDL2 (pinned version + sha256) into `android/deps/SDL2` (git-ignored);
2. fetches the Keen 4 v1.4 shareware with `tools/fetch-k4-shareware.sh` into
   `android/app/src/main/assets/game/keen4/` (git-ignored; never commit game data) and runs
   `tools/check-no-data.sh`;
3. runs `./gradlew assembleDebug` and checks the APK holds `libmain.so` + `libSDL2.so` for
   arm64-v8a and x86_64, the shareware, and the Omnispeak tables, and no Keen 5/6 data.

The build is heavy (NDK, two ABIs): run it on a droplet (`tools/droplet.sh`), not on the
dev host. Release signing is not set up yet (debug APK only).

## How it fits together

- `app/jni/CMakeLists.txt` adds SDL2 from source (as SDL's `android-project` template does)
  and then the repo's top-level `CMakeLists.txt`, which builds the engine as the shared
  library `libmain.so` when `ANDROID` is set (`main` is renamed to `SDL_main` for
  `src/ck_main.c`). `app/jni/keen_android.c` is the JNI bridge.
- SDL's Java glue (`org.libsdl.app`) is compiled straight from the fetched SDL tree, so it
  always matches the native SDL. `org/libsdl/app/KeenGlue.java` is our only addition there.
- The Omnispeak tables from `data/` (keen4, keen5, keen6e14, keen6e15) are copied into the
  APK as `assets/omni/` by the Gradle task `copyOmniTables`.

## The app

- **PickerActivity** (launcher): three episode cards. Keen 4 shareware is bundled and plays
  at once. Keen 5/6, and the registered Keen 4, are imported from the player's own files
  through the Storage Access Framework (folder or .zip): only `EGAGRAPH`, `GAMEMAPS`,
  `AUDIO.CK<n>` are copied into app storage. Keen 6 v1.4 / v1.5 is detected from the size of
  `EGAGRAPH.CK6`. ⚙ Settings.
- **KeenActivity** (an `SDLActivity`, own `:game` process) runs the engine with
  `/EPISODE n /GAMEPATH <game dir> /USERPATH <save dir>` plus the settings; by default
  `/PROFILE enhanced /INTERP subpixel /MODERNCONTROLS /POGOHOLD 180 /FILTER off /PIXELS crisp
  /ASPECT 4:3 /NOXRAY /VSYNC`. Options with no switch are written into `OMNISPK.CFG` before
  launch (`ck_quickSaveConfirm`, from the QuickSave confirmation setting).
  Before launch, `GameSetup.prepare` copies the matching table set next to the game files.
- Storage (external app files dir, reachable over USB): `game/keen<n>/` data,
  `save/keen<n>/` savegames and `omnispeak.cfg` (imports never touch saves).
- Display: portrait only (both activities are locked to portrait; there is no landscape
  layout). SDL's surface is sized to a 4:3 pane at the top; the engine fills it
  through the SDL2 GLES renderer with vsync; the touch overlay owns the rest of the screen.
  The activity asks for the panel's fastest refresh mode.

## Controls

Stick = arrow keys (8-way) · JUMP = Ctrl · SHOOT = Space · POGO = the engine's modern pogo
(`IN_SetTouchPogo` in `src/id_in.c`: pogo, then jump ~30 ms later while held for the
super-bounce; holding longer than the retract time retracts it on release) · XR = X-ray on/off
(lit while on) · 💾 = SAVE / LOAD popup: QuickSave (F5) / QuickLoad (F9), no Y/N prompt unless
turned on in settings · ESC, ⏎, Y, N as labelled · ⌨ soft keyboard (save names) · ⚙ settings ·
Back = Esc. A hardware keyboard or gamepad works as on desktop.

Settings (⚙, picker or in game; applied at the next episode start): smoothing, screen filter,
pixels, camera smoothing, profile, pogo retract, X-ray at start, QuickSave confirmation,
hardware-keyboard Alt, fullscreen.

## Not done yet

Release signing, save sync (the DOSBox-X app's Sync tab), a turbo/FF button (no engine
counterpart), gamepad mapping UI, background/resume testing on devices. Landscape is not planned.
