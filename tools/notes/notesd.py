#!/usr/bin/env python3
"""notesd - turn design notes said in the headset into text for the coding agent
(docs/NOTES.md).

    tools/notes/notesd.py [--app toolbox] [--watch DIR ...] [--once] [--interval 2]

Every few seconds it
  1. pulls new notes from the headset (scripts/frame.sh notes <app>: the .wav,
     .json and .png sfxr_note_end() wrote; only what isn't here yet), and looks
     in any --watch directories too (an app run on this machine writes its
     notes/ next to itself: the simulator, tests);
  2. transcribes each new .wav with the whisper-server (scripts/notes.sh starts
     it: whisper.cpp with a large model on this machine's GPU);
  3. appends one JSON line per note to local-data/notes/<app>.jsonl: the text,
     the context the app gave, where you were, and the screenshot's path. That
     file is what the channel (tools/notes/channel.ts) hands to Claude Code.

It needs only the standard library. Settings: SFQ_WHISPER_URL (default
http://127.0.0.1:9878), SFQ_NOTES_PROMPT (words to expect, added to the
transcriber's prompt).
"""
import argparse
import json
import os
import subprocess
import sys
import time
import urllib.request
import uuid

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WHISPER = os.environ.get("SFQ_WHISPER_URL", "http://127.0.0.1:9878")
OUT = os.path.join(ROOT, "local-data", "notes")


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def transcribe(wav, prompt):
    """POST the wav to whisper-server's /inference; the text, or None."""
    boundary = "----sfq" + uuid.uuid4().hex
    fields = {"response_format": "json", "temperature": "0.0", "no_timestamps": "true"}
    if prompt:
        fields["prompt"] = prompt
    body = b""
    for k, v in fields.items():
        body += f"--{boundary}\r\nContent-Disposition: form-data; name=\"{k}\"\r\n\r\n{v}\r\n".encode()
    with open(wav, "rb") as f:
        data = f.read()
    body += (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"{os.path.basename(wav)}\"\r\n"
             "Content-Type: audio/wav\r\n\r\n").encode() + data + b"\r\n"
    body += f"--{boundary}--\r\n".encode()
    req = urllib.request.Request(WHISPER + "/inference", data=body,
                                 headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            res = json.loads(r.read().decode("utf-8", "replace"))
    except Exception as e:  # noqa: BLE001
        log("whisper:", e)
        return None
    text = res.get("text", "") if isinstance(res, dict) else ""
    text = " ".join(text.split())
    # Whisper's no-speech markers
    if text.startswith("[") or text.startswith("("):
        text = ""
    return text


def pull_from_frame(app, dest):
    """scripts/frame.sh notes: new note ids (the files land in dest)."""
    try:
        out = subprocess.run([os.path.join(ROOT, "scripts", "frame.sh"), "notes", app, dest],
                             capture_output=True, text=True, timeout=60)
    except Exception as e:  # noqa: BLE001
        log("frame.sh notes:", e)
        return []
    if out.returncode != 0:
        return []
    return [l.strip() for l in out.stdout.splitlines() if l.strip()]


def pending(dirs, done):
    """(dir, id) for every note with a .json and .wav we haven't transcribed."""
    found = []
    for d in dirs:
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if not name.endswith(".json"):
                continue
            nid = name[:-5]
            key = os.path.join(d, nid)
            if key in done or not os.path.exists(os.path.join(d, nid + ".wav")):
                continue
            found.append((d, nid))
    return found


def process(app, d, nid, prompt, done_path):
    with open(os.path.join(d, nid + ".json")) as f:
        note = json.loads(f.readline())
    text = transcribe(os.path.join(d, nid + ".wav"), prompt)
    if text is None:
        return False   # try again next round
    note["app"] = app
    note["text"] = text
    note["dir"] = d
    note["screenshot"] = os.path.join(d, note.get("screenshot", nid + ".png"))
    if not os.path.exists(note["screenshot"]):
        note["screenshot"] = None
    note["transcribed_at"] = time.strftime("%Y-%m-%dT%H:%M:%S")
    with open(os.path.join(d, nid + ".txt"), "w") as f:
        f.write(text + "\n")
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, app + ".jsonl"), "a") as f:
        f.write(json.dumps(note) + "\n")
    with open(done_path, "a") as f:
        f.write(os.path.join(d, nid) + "\n")
    log(f"{app} {nid} ({note.get('seconds', 0)} s): {text!r}")
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", default="toolbox", help="the app on the headset (default toolbox)")
    ap.add_argument("--no-frame", action="store_true", help="don't pull from the headset")
    ap.add_argument("--watch", action="append", default=[], help="a local notes/ directory to watch too (repeatable)")
    ap.add_argument("--once", action="store_true")
    ap.add_argument("--interval", type=float, default=2.0)
    args = ap.parse_args()

    prompt = os.environ.get("SFQ_NOTES_PROMPT", "")
    dest = os.path.join(OUT, args.app)
    os.makedirs(dest, exist_ok=True)
    done_path = os.path.join(OUT, args.app + ".done")
    done = set()
    if os.path.exists(done_path):
        done = {l.strip() for l in open(done_path) if l.strip()}
    dirs = [dest] + [os.path.abspath(w) for w in args.watch]
    log(f"notesd: app {args.app}, whisper {WHISPER}, watching {dirs}" + ("" if args.no_frame else " + the headset"))
    while True:
        if not args.no_frame:
            pull_from_frame(args.app, dest)
        for d, nid in pending(dirs, done):
            if process(args.app, d, nid, prompt, done_path):
                done.add(os.path.join(d, nid))
        if args.once:
            return
        time.sleep(args.interval)


if __name__ == "__main__":
    main()
