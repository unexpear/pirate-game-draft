# Steamworks (optional bolt-on)

Steamworks is the one proprietary dependency. It is **not** in this
repository, and the default build does not look for it.

`SEA_TRIAL_WITH_STEAM` defaults to **OFF**. The `steam_platform` façade
compiles either way:

| Build | What you get |
|---|---|
| OFF (default, CI) | Local stubs. `init` succeeds, `isSteam()` is false, `userShipsRoot()` is `<cwd>/user_ships`. `cloudSyncUserShips` succeeds and does not touch that folder. No SDK on the machine. |
| ON | CMake links a Steamworks SDK you supply. If that SDK is missing, configure fails and names the path it searched. If the SDK links but the Steam client is not running, the process stays offline and keeps the local `user_ships` folder. If a session is up, `cloudSyncUserShips` mirrors that folder's `.ship.json` files through Steam Cloud, and **Publish** can submit one new private Workshop item. |

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

The debug panel shows `Steam: offline (local)` or `Steam: <persona>`, the
ships folder, and a **Sync ships** button.

The AppID also needs Steam Cloud turned on in the Steamworks partner site
before `FileWrite` will reach the backend. AppID `480` (Spacewar) is the
usual stand-in and already has Cloud. This slice does not call
`SetCloudEnabledForApp`; if the player has disabled Cloud for the account
or the app, sync reports that and leaves ships local.

## Local folder and Steam Cloud

`userShipsRoot()` is always `<cwd>/user_ships`. That is the working copy
Build and Sail read and write (see [vertical-slice.md](../vertical-slice.md)).
Steam Cloud is not a second directory. `ISteamRemoteStorage` stores objects
by name, so the file `<cwd>/user_ships/test_sloop.ship.json` is the cloud
object `user_ships/test_sloop.ship.json`.

`cloudSyncUserShips` is the only transfer:

| Situation | What sync does |
|---|---|
| Steam off, or the client is not running | Success. Message says the ships are local. The folder is not read or written. |
| Session up, Cloud disabled for the account or the app | Success. Ships stay local. Cloud is not written. |
| Session up, Cloud enabled | List cloud files, read and write `.ship.json` only. |

Conflict rules (`planCloudFile`), per file:

- Only on disk → upload.
- Only in Cloud → download into `userShipsRoot()`.
- Both, same bytes → leave them.
- Both, different bytes → the newer Unix mtime wins. Equal timestamps keep
  the local file and upload it.
- Names are matched with ASCII case-folding because Steam stores cloud names
  in lowercase. Two local files that differ only by case are skipped, not
  uploaded.
- A cloud file whose name is not `user_ships/<one segment>.ship.json` is
  ignored. Separators and `..` are rejected so a listing cannot write
  outside the ships folder.
- Deletes are not synced. A ship removed on only one side is copied back
  on the next sync.
- Files over 1 MiB are skipped. Workshop uses the same 1 MiB prototype cap
  (see below). Shipping Workshop limits are still open in [interop.md](../interop.md).

Sync is synchronous on the main thread (`FileRead` / `FileWrite`). Ship
files are small. The app calls it once after `init`, and the debug panel
button calls it again. `init` itself does not talk to Cloud, so the
headless self-test can stand the façade up without a round-trip. Call
`cloudSyncUserShips` again after a save lands in the folder. The Workshop
button writes the current ship into `user_ships/` before it checks it;
cloud sync does not do that write itself.

## Workshop validate and publish

`workshopValidate(shipPath)` is the local half of "validated on upload and on download" ([interop.md](../interop.md)). It always runs, including the default build with Steam off. It does not publish.

The file is schema v1 JSON (`.ship.json`, see [vertical-slice.md](../vertical-slice.md)). The model's line-oriented `serialize` dump is a different format and is not what this check reads. `toShipJson` writes the Workshop file; the debug panel uses it when you press **Validate ship for Workshop**, which saves the current ship into `user_ships/` and then checks that file.

The check is `sea::validateShip` plus a file-size gate:

| Code | When |
|---|---|
| `OK` | The file passed. Warnings (`NARROW_HULL`, `LOW_FLOAT_MARGIN`, `HIGH_DAMAGE`) do not fail the check. |
| `BAD_PATH` | Empty path, or the path is not a regular file. |
| `NOT_A_SHIP` | The file name is not one `*.ship.json` segment. |
| `CANNOT_READ` | Missing or unreadable. |
| `EMPTY_FILE` | Zero bytes. |
| `FILE_TOO_LARGE` | Larger than 1 MiB. |
| `BAD_JSON` | Not schema-v1 JSON, or a field has the wrong shape. |
| `BAD_SCHEMA` | `schema_version` is not 1. |
| `NO_PIECES` | No pieces. |
| `MISSING_HELM` / `MISSING_SAIL` | Helm count or sail count is below 1. |
| `BAD_MASS` / `BAD_VOLUME` / `BAD_ID` | Mass, volume, or a piece id is unusable. |
| `DOES_NOT_FLOAT` | Loaded weight is not less than buoyancy. |
| `PIECE_CAP` | More than 220 pieces. |
| `BOUNDS_CAP` | Length above 26 m, beam above 9 m, or height above 10 m. |

Those caps are the prototype numbers already in the ship validator. Shipping thresholds are still open in [holes.md](../holes.md). The 1 MiB cap is the same guard Steam Cloud uses in this prototype.

`workshopPublish(shipPath)` is a separate step:

| Situation | What publish does |
|---|---|
| Local check fails | `Failed`, with the same error codes. Nothing is submitted. |
| Check passes, Steam off or no live session | `Unimplemented`, code `NOT_SUBMITTED`. The message says the ship was validated locally. Nothing is published. |
| Check passes and a session is up | `ISteamUGC::CreateItem`, then `StartItemUpdate` / `SetItemContent` / `SubmitItemUpdate`. On success, code `PUBLISHED`. The item is **private**. Each click creates a new item; this slice does not store the published file id and does not update an existing one. |

Submit is synchronous on the main thread, same as cloud sync, and gives up after 30 seconds. `WORKSHOP_LEGAL` means the player still has to accept the Workshop agreement. `WORKSHOP_UNAVAILABLE` and `SUBMIT_FAILED` mean the session was up and the submit did not finish. If `CreateItem` succeeds and a later step fails, an empty private item can remain on the Workshop. There is no Workshop browser and no subscription download. A future download path should call `workshopValidate` before it loads a ship.

The debug panel shows **Validate ship for Workshop** on every build. **Publish** is enabled only while `isSteam()` is true; otherwise the button reads **Publish (Steam off)** and does not click.

## Façade

`native/src/steam_platform.h`

- `init` / `shutdown` / `pumpCallbacks` — called from `main` every run,
  including the stub build.
- `isSteam` / `personaName`
- `userShipsRoot` — always `<cwd>/user_ships`, offline and online.
- `cloudSyncUserShips` / `cloudSyncMessage` — local no-op when offline;
  `ISteamRemoteStorage` list + read/write when a session is up.
- `planCloudFile`, `isUserShipFileName`, `cloudObjectName`,
  `cloudObjectBasename` — the sync rules, with no I/O, so the default build
  can test them.
- `workshopValidate` / `workshopMessage` / `lastWorkshopResult` — local `.ship.json` check. Ok or Failed, with the codes above. No Workshop network.
- `workshopPublish` — local check first. `NOT_SUBMITTED` without a session. With a session, one new private `ISteamUGC` item (`PUBLISHED`).

## Default build (what CI runs)

```bash
cmake -S native -B native/build -DSEA_TRIAL_BUILD_APP=OFF
cmake --build native/build --target sea_trial_selftest
ctest --test-dir native/build --output-on-failure
```

Steam stays off. The self-test checks that stub `init` succeeds, that
`userShipsRoot` is the local `user_ships` directory, that offline
`cloudSyncUserShips` succeeds without changing a `.ship.json` (or a
non-ship file) in that folder, and that the name and conflict rules above
choose upload, download, or leave-alone. It also checks `workshopValidate`
on a generated sample ship and on synthetic JSON (pass, missing helm,
missing sail, does not float, piece cap, bounds, file size, bad JSON), and
that offline `workshopPublish` returns `NOT_SUBMITTED` after a passing
check and does not submit an invalid ship.

The `ISteamRemoteStorage` and `ISteamUGC` calls are compiled only when
`SEA_TRIAL_WITH_STEAM` is on. CI never links the SDK.

## What comes next

- **Workshop browser.** Subscriptions, downloading a subscribed ship (then
  `workshopValidate` before it loads), and updating an item that was
  already published. This slice only validates locally and, with a
  session, creates one new private item.
- **Not this slice:** SDR, lobbies, voice, multiplayer, host migration.
  Those stay out until the single-player Build → Sail loop wants them
  (see [multiplayer-infra.md](../multiplayer-infra.md)).
