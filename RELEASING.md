# Releasing

Releases are cut from git tags. Pushing a semver tag `vMAJOR.MINOR.PATCH` runs
`.github/workflows/release.yml`, which builds every platform and publishes **one** GitHub Release.

## Cut a release

```sh
git switch main && git pull
git tag -a v1.2.3 -m "keen456 1.2.3: <one line about what is new>"
git push origin v1.2.3
```

Watch it: `gh run watch -R awkto/keen456-omnispeak-fork` (about 10 minutes). When it is green the release is at
<https://github.com/awkto/keen456-omnispeak-fork/releases/latest>.

- **`vX.Y.Z`**: a normal release. It becomes "latest" and the web build is deployed to GitHub Pages
  (<https://awkto.github.io/keen456-omnispeak-fork/>).
- **`vX.Y.Z-rc1`** (anything with a `-suffix`): a pre-release. It is never "latest" and does not
  touch Pages: use it to test the pipeline or hand a build to testers.
- Re-running a failed release run (or pushing the same tag again after deleting it) replaces the assets.

## What the release contains

| Asset | What |
|---|---|
| `keen456-omni.apk` | Android (arm64 + x86_64), signed with the stable key: signer SHA-256 `a6629253…` (CI fails on any other key) |
| `keen456-linux-x86_64.tar.gz` | `keen456` single-file launcher (engine + SDL2, glibc 2.35+), README, LICENSE |
| `keen456-windows-x86_64.zip` | `keen456\` folder: `omnispeak.exe` (static MinGW), `keen456.exe` launcher, `Keen 4/5/6.bat` |
| `keen456-web.tar.gz` | the WebAssembly site, identical to what Pages serves |
| `SHA256SUMS` | `sha256sum -c SHA256SUMS` |

Asset names carry no version, so these links always point at the newest release:
`https://github.com/awkto/keen456-omnispeak-fork/releases/latest/download/<asset>`.

Versions: the APK's `versionName` is the tag without the `v` (`1.2.3`); `versionCode` is the commit count
(`git rev-list --count`), so every later release installs over an earlier one. The Linux and Windows
launchers print the same version in `--help`. The release notes list the commits since the previous release tag.

## Game data (hard rule)

Only the **Keen 4 shareware** may appear in a public build, release, log or the Pages site. Keen 5 and 6
are commercial: players supply their own files (`--data DIR`, `KEEN_DATA`, the per-user data folder,
Android import, or the web page's file picker). Every asset is checked by `tools/check-public-artifact.sh`
(and the site by `tools/check-pages-artifact.sh`) before it is published. Private builds with Keen 5/6
bundled are made locally only: `tools/package-linux-single.sh private`, `tools/package-windows-single.sh private`.

## CI on main and pull requests

`.github/workflows/ci.yml`: engine build, `tools/check-no-data.sh`, Keen 4 demo dump identity
(`tests/demoN.dump4`), `tools/vanilla-guard.sh`, then an unsigned build of every package (`build.yml`, the
same jobs the release uses). Keen 5/6 demo dumps need the commercial data and run locally only.
`pages.yml` can redeploy the site by hand (Actions > "Pages: manual web deploy").

## Signing key

The APK key lives in the repository secret `ANDROID_KEYSTORE_B64` (base64 of the kept debug keystore; the
owner's copy is `~/.config/keen456/debug.keystore`). Losing it means users must uninstall (and lose saves)
to update, so never rotate it casually.
