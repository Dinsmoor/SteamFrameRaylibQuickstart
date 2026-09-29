---
name: deploy
description: Build an app for the Steam Frame, push it to the headset through the devkit pairing, launch it via Steam and confirm it's rendering; pull logs, screenshots and notes back. Use for "deploy", "put it on the headset", "run it in the headset", "frame-go".
---

Everything goes through `scripts/frame.sh` and the paired key; never raw ssh
or passwords to the headset (CLAUDE.md). The headset must be on, in Developer
Mode, with Steam running; the developer may be wearing it.

1. **Build for the Frame:** `make frame` (release, inside Valve's Steam Runtime
   SDK container; a few minutes the first time). Fix compile errors here, not
   on the device. Build assets first if any changed (`make assets`).
2. **Deploy:** `scripts/frame.sh deploy <app>` packages `dist/<app>/` (binary,
   launch.sh, resources, the speech model) and rsyncs it; `notes/`, logs and
   recordings on the device are kept.
3. **Run:** `scripts/frame.sh run <app> 30` launches through Steam and follows
   `logs/run.log` for 30 s. Read the heartbeat: `session=FOCUSED
   should_render=1` means frames reach the eyes; `SYNCHRONIZED` or
   `should_render=0` means nobody is wearing it or the dashboard is up;
   nothing after `session state -> IDLE` means the runtime never started.
   Report what you saw, not "deployed".
4. **See it:** `scripts/frame.sh shot <app> 300 SFXR_RIG=0,0.3,1.2,20` pulls a
   both-eye PNG (works only while it renders, so while it's worn). Look at it.
5. **Afterwards:** `scripts/frame.sh notes <app>` (new design notes; notesd
   does this on its own when running), `scripts/frame.sh pull <app>`
   (recordings, events, logs), `scripts/frame.sh stop <app>`.

If `frame.sh` says not paired, or the host isn't reachable, say so and stop:
pairing needs someone at the headset (`make frame-pair`).
