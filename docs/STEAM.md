# Steamworks: achievements, stats, the overlay

`sfxr_steam.h` is an optional, small bridge to Steamworks. It covers:
- achievements
- integer stats
- the player's name and the app id
- whether the Steam overlay is open

It builds without the Steamworks SDK, and without Steam every call is a harmless "no", so
the same binary runs on your desk, in the simulator and on the headset.

```c
sfxr_steam_init();                               // once, after sfxr_init (the toolbox does this)
if (climbed_the_wall) sfxr_steam_unlock("FIRST_CLIMB");
if (sfxr_steam_overlay_active()) pause_game();
```

The toolbox's Headset panel shows Steam's status and has a test-achievement button.

## Why it works this way

- **The SDK stays out of the repo.** Valve's Steamworks SDK license lets you ship
  `libsteam_api.so` with your built game. It doesn't let you publish the SDK in a public
  repository. And a quickstart has to build for someone with no Steamworks account yet.
- So sfxr **loads the library at run time** (`dlopen`) and calls Steamworks' *flat C API*.
  The flat API takes plain C types, and its interfaces are opaque pointers, so sfxr declares
  the dozen functions it uses itself. No SDK headers are needed.
- **Names that changed between SDK versions are tried newest first:** the init function
  (`SteamAPI_InitFlat`, then the older forms) and the versioned interface accessors
  (`SteamAPI_SteamUserStats_v013` / `v012`...).
- **Callbacks use manual dispatch**, the C way: each frame, `sfxr_frame_begin` pulls pending
  callbacks and frees every one, handled or not. That's how the overlay state arrives.

## Setting it up

1. Get the Steamworks SDK from the Steamworks partner site. Research at the time of writing
   says the Frame (ARM64) needs **1.64 or newer**, the first with a `linuxarm64`
   library suited to its memory pages. Check the SDK's release notes.
2. Package with it:
   ```
   STEAMWORKS_SDK=~/sdk/steamworks make package EX=toolbox
   ```
   That copies `redistributable_bin/linuxarm64/libsteam_api.so` next to the binary, where
   sfxr looks first, and writes `steam_appid.txt`.
3. **The app id:**
   - `steam_appid.txt` says which Steam app you are when Steam didn't launch you under one.
   - The default is **480**: Spacewar, Valve's public test app, which has achievements such
     as `ACH_WIN_ONE_GAME`.
   - Set your own with `SFQ_STEAM_APPID=...`.
   - Leave the file out of a real Steam release: Steam sets the id itself there.
4. `SFQ_STEAM_LIB=/path/to/libsteam_api.so` overrides where the library is loaded from.

## What's verified and what isn't

- **Verified here:** loading, the fallbacks, callback dispatch, storing, and "absent is
  harmless". `tests/steam` checks each against a **fake `libsteam_api.so`**
  (`tests/steam/fake/steam_api.c`) that has the same function names and C types, and
  records what was called:

  | Case | Break switch |
  |---|---|
  | `steam/unlock-is-stored` | `sfxr_steam_no_store` |
  | `steam/overlay-callback-arrives` | `sfxr_steam_no_dispatch` |
  | `steam/absent-is-harmless` | `sfxr_steam_claims_success` |
  | `steam/loads-and-reports` | none |

- **Not verified yet:** the real library on the Frame.
  - Does a devkit launch (a "Devkit Game" registered by `frame.sh deploy`) get a Steam
    connection at all?
  - Does 480 plus `steam_appid.txt` work there?
  - Does the achievement popup show in the headset?

  The first headset session with the SDK answers all three: press the Headset panel's test button.
  `logs/run.log` prints the status line, "Steam: on, app 480, player ..." or why not.

## Gotchas

- **Unlocking is two steps in Steamworks:** *set* changes the local copy, *store* sends it.
  `sfxr_steam_unlock` does both. A set without a store is the classic "achievement never
  appears" bug, and it has a test.
- **Every callback must be freed** in manual dispatch, even ones you ignore. Otherwise the
  queue stalls.
- **On the Frame, the Steam overlay is not the SteamVR dashboard.** The dashboard shows up as
  `sfxr_focused()` going false. Pause on either.
