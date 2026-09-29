# Steamworks (optional bolt-on)

Steamworks is the one proprietary dependency. It is **not** in this
repository, and the default build does not look for it.

`SEA_TRIAL_WITH_STEAM` defaults to **OFF**. The `steam_platform` façade
compiles either way:

| Build | What you get |
|---|---|
| OFF (default, CI) | Local stubs. `init` succeeds, `isSteam()` is false, `userShipsRoot()` is `<cwd>/user_ships`. No SDK on the machine. |
| ON | CMake links a Steamworks SDK you supply. If that SDK is missing, configure fails and names the path it searched. If the SDK links but the Steam client is not running, the process stays offline and keeps the local `user_ships` folder. |

The SDK's own license stays with the SDK on your machine. Do not commit the
SDK tree, `steam_api.dll` / `steam_api64.dll`, `libsteam_api.so`,
`libsteam_api.dylib`, or `steam_appid.txt`.

## Drop in the SDK

1. Download the Steamworks SDK with a Steamworks partner account:
   <https://partner.steamgames.com/doc/sdk>
2. Point the build at the extracted tree in **one** of these ways (first
   match wins: CMake variable, then environment variable, then the drop-in
   directory):

   **Drop-in (gitignored).** Copy either the SDK's `sdk` folder or the zip
   root into `native/extern/steamworks/` so that one of these files exists:

   - `native/extern/steamworks/public/steam/steam_api.h`
   - `native/extern/steamworks/sdk/public/steam/steam_api.h`

   **Or pass a path** (the same two layouts work):

   ```bash
   cmake -S native -B native/build \
     -DSEA_TRIAL_WITH_STEAM=ON \
     -DSTEAMWORKS_SDK=/path/to/sdk
   ```

   ```bash
   export STEAMWORKS_SDK=/path/to/sdk
   cmake -S native -B native/build -DSEA_TRIAL_WITH_STEAM=ON
   ```

3. Build as usual. CMake links `redistributable_bin/{win64,linux64,osx}` for
   the host and copies the shared library next to the executable
   (`steam_api64.dll`, `libsteam_api.so`, or `libsteam_api.dylib`).

If `-DSEA_TRIAL_WITH_STEAM=ON` and none of those locations contain
`steam_api.h`, CMake stops with the path it used and how to set the others.

## Run against the Steam client

`SteamAPI_Init` reads `steam_appid.txt` (one line, the AppID) from the
process working directory when you launch outside Steam. Valve's Spacewar
sample is AppID `480` and is the usual stand-in before this game has its
own. The file is gitignored. This slice does not call
`SteamAPI_RestartAppIfNecessary`, so a missing client does not relaunch
the process; the game continues offline.

The debug panel shows `Steam: offline (local)` or `Steam: <persona>`.

## Façade

`native/src/steam_platform.h`

- `init` / `shutdown` / `pumpCallbacks` — called from `main` every run,
  including the stub build.
- `isSteam` / `personaName`
- `userShipsRoot` — always `<cwd>/user_ships` in this slice. That is the
  local stand-in for the slice's `/user_ships/` folder (see
  [vertical-slice.md](../vertical-slice.md)). It is not a Steam Cloud path yet.
- `cloudSyncUserShips` / `workshopValidate` — return `Unimplemented`. No
  sockets, no upload.

## Default build (what CI runs)

```bash
cmake -S native -B native/build -DSEA_TRIAL_BUILD_APP=OFF
cmake --build native/build --target sea_trial_selftest
ctest --test-dir native/build --output-on-failure
```

Steam stays off. The self-test checks that stub `init` succeeds and that
`userShipsRoot` is the local `user_ships` directory.

## What comes next

- **Steam Cloud.** Sync the `user_ships/` folder (`ISteamRemoteStorage`, or
  Steam auto-cloud) and teach `userShipsRoot` to return that cloud-backed
  directory when a session is up.
- **Workshop.** Upload and download, with the validation rules in
  [interop.md](../interop.md) (piece cap, bounds, file size, must float,
  helm + sails) on both upload and download. `workshopValidate` is the hook.
- **Not this slice:** SDR, lobbies, voice, multiplayer, host migration.
  Those stay out until the single-player Build → Sail loop wants them
  (see [multiplayer-infra.md](../multiplayer-infra.md)).
