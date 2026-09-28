#!/usr/bin/env python3
"""Stand-in for the Steam client's devkit IPC and the headset's devkit pairing
service, for rehearsal only.

Pairing (like the real headset's service on :32000):
  GET  /properties.json -> {"login": "steamos", "devkit1": ["devkit-1"], ...}
  POST /register        -> body "<ssh-rsa key devkit-client:...> <magic>"; auto-approved
                           (a real headset asks the wearer), key added to authorized_keys.
                           Non-RSA keys are rejected exactly like the real service does.


Implements the protocol from Valve's devkit-utils (tools/devkit-utils):
  pipe line: 'devkit-1 steam://devkit-1/<token>/<command>?response=<path>&k=v'
  reply:     write <path>.lock, then <path> (or <path>.error), remove the lock.
Commands: create-shortcut (record the game), run-game (spawn argv in the
game's directory with the saved env, like Steam launching a Devkit Game).
"""
import http.server, json, os, secrets, subprocess, sys, threading, time, urllib.parse

HOME = os.path.expanduser('~')
STEAM = os.path.join(HOME, '.steam')
GAMES = os.path.join(HOME, 'devkit-game')
os.makedirs(STEAM, exist_ok=True)
pipe = os.path.join(STEAM, 'steam.pipe')
if os.path.exists(pipe):
    os.remove(pipe)
os.mkfifo(pipe)
open(os.path.join(STEAM, 'steam.pid'), 'w').write(str(os.getpid()))
token = secrets.token_hex(8)
open(os.path.join(STEAM, 'steam.token'), 'w').write(token)
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
log('fake steam up, pid', os.getpid())

MAGIC = '900b919520e4cf601998a71eec318fec'

class Devkit(http.server.BaseHTTPRequestHandler):
    def _send(self, code, body, ctype='application/json'):
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.end_headers()
        self.wfile.write(body.encode())

    def do_GET(self):
        if self.path == '/properties.json':
            self._send(200, json.dumps({'txtvers': 1, 'login': 'steamos', 'settings': '{}', 'devkit1': ['devkit-1']}))
        elif self.path == '/login-name':
            self._send(200, 'steamos', 'text/plain')
        else:
            self._send(404, '{}')

    def do_POST(self):
        if self.path != '/register':
            return self._send(404, '{}')
        body = self.rfile.read(int(self.headers.get('Content-Length', 0))).decode()
        parts = body.split()
        if len(parts) < 3 or parts[0] != 'ssh-rsa' or parts[-1] != MAGIC or not parts[2].startswith('devkit-client:'):
            return self._send(200, json.dumps({'error': 'Failed to write the ssh key'}))
        key = ' '.join(parts[:-1])
        d = os.path.join(HOME, '.ssh')
        os.makedirs(d, mode=0o700, exist_ok=True)
        with open(os.path.join(d, 'authorized_keys'), 'a') as f:
            f.write(key + '\n')
        os.chmod(os.path.join(d, 'authorized_keys'), 0o600)
        log('paired key', parts[2])
        self._send(200, 'Registered', 'text/plain')

    def log_message(self, *a):
        pass

threading.Thread(target=http.server.ThreadingHTTPServer(('0.0.0.0', 32000), Devkit).serve_forever, daemon=True).start()
log('devkit pairing service on :32000')

def reply(path, text, error=False):
    open(path + '.lock', 'w').close()
    open(path + ('.error' if error else ''), 'w').write(text)
    os.remove(path + '.lock')

def load(gameid, kind, default):
    p = os.path.join(GAMES, f'{gameid}-{kind}.json')
    return json.load(open(p)) if os.path.exists(p) else default

while True:
    with open(pipe, 'r') as f:
        for line in f:
            line = line.strip()
            log('pipe:', line)
            try:
                _, url = line.split(' ', 1)
                rest = url.split(f'/{token}/', 1)[1]
                cmd, _, query = rest.partition('?')
                cmd = cmd.strip('/')
                q = dict(urllib.parse.parse_qsl(query))
                resp, gameid = q.get('response'), q.get('gameid')
                if cmd == 'create-shortcut':
                    settings = load(gameid, 'settings', {})
                    reply(resp, f'Registered "Devkit Game: {gameid}" (compat_tool={settings.get("settings", settings).get("compat_tool")})')
                elif cmd == 'run-game':
                    d = os.path.join(GAMES, gameid)
                    argv = load(gameid, 'argv', ['./launch.sh'])
                    env = dict(os.environ, **load(gameid, 'env', {}) or {})
                    subprocess.Popen(argv[0].split() + argv[1:], cwd=d, env=env)
                    reply(resp, f'launched {gameid}')
                else:
                    reply(resp, f'unsupported command {cmd}', error=True)
            except Exception as e:
                log('error:', e)
