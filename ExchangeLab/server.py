#!/usr/bin/env python3
"""Local-only HTTP bridge. Python serves UI; every fill is computed by C++."""
import argparse
import json
import os
import platform
import secrets
import subprocess
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
WEB = ROOT / 'web'
MAX_BODY = 262144

class Session:
    def __init__(self):
        self.lock = threading.RLock()
        self.process = subprocess.Popen([str(ROOT / 'build' / 'exchange'), '--json'],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        text=True, bufsize=1)
        self.journal = []
        self.events = []
        self.replay = []
        self.cursor = 0
        self.benchmark = None
        self.latest = self.send('BOOK', record=False)

    def send(self, command, record=True):
        if self.process.poll() is not None:
            raise RuntimeError('Engine stopped. Restart the local server.')
        self.process.stdin.write(command + '\n')
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError('Engine produced no response.')
        result = json.loads(line)
        if record:
            self.journal.append(command)
            self.events.append({'number': len(self.journal), 'command': command,
                                'status': result['status'], 'filled': result['filled'],
                                'cancelled': result['cancelled'], 'message': result['message']})
            self.events = self.events[-100:]
        self.latest = result
        return result

    def state(self):
        data = dict(self.latest)
        data.update(events=self.events, event_count=len(self.journal),
                    replay_cursor=self.cursor, replay_count=len(self.replay),
                    benchmark=self.benchmark)
        return data

    def reset(self):
        self.send('RESET', record=False)
        self.journal.clear()
        self.events.clear()
        self.replay.clear()
        self.cursor = 0

    def commands(self, text):
        if not isinstance(text, str):
            raise ValueError('Commands must be text')
        lines = [line.strip() for line in text.splitlines()
                 if line.strip() and not line.strip().startswith('#')]
        if not lines or len(lines) > 5000:
            raise ValueError('Use 1 to 5000 commands')
        for line in lines:
            parts = line.split()
            if len(line) > 512 or not line.isascii():
                raise ValueError('Commands must be ASCII and at most 512 characters')
            if parts[0] not in ('BUY', 'SELL', 'CANCEL', 'RESET'):
                raise ValueError('Replay supports BUY, SELL, CANCEL, RESET only')
        return lines

    def action(self, data):
        action = data.get('action')
        if action == 'benchmark':
            output = subprocess.run([str(ROOT / 'build' / 'benchmark')],
                                    capture_output=True, text=True, timeout=90, check=True)
            result = json.loads(output.stdout)
            result.update(machine=platform.machine(), system=platform.system(),
                          platform=platform.platform(), cpu_count=os.cpu_count(),
                          build_flags='-std=c++17 -O2',
                          note='Local instrumented microbenchmark; excludes HTTP, JSON and UI. Synthetic seeded workload; not exchange latency.')
            with self.lock:
                self.benchmark = result
                return self.state()
        with self.lock:
            if action == 'command':
                commands = self.commands(data.get('command', ''))
                if len(commands) != 1 or commands[0] == 'RESET':
                    raise ValueError('Use one BUY, SELL or CANCEL command')
                if len(self.journal) >= 10000:
                    raise ValueError('Dashboard session limit reached; reset first')
                self.send(commands[0])
            elif action == 'reset':
                self.reset()
            elif action in ('seed', 'scenario'):
                scenario = data.get('scenario', 'baseline') if action == 'scenario' else 'baseline'
                names = {'baseline': 'baseline.txt', 'sweep': 'sweep.txt',
                         'fifo': 'fifo.txt', 'fok': 'fok.txt'}
                if scenario not in names:
                    raise ValueError('Unknown scenario')
                commands = self.commands((ROOT / 'data' / names[scenario]).read_text())
                self.reset()
                for command in commands:
                    self.send(command)
            elif action == 'load_replay':
                commands = self.commands(data.get('text', ''))
                self.reset()
                self.replay = commands
            elif action == 'step':
                if self.cursor < len(self.replay):
                    self.send(self.replay[self.cursor])
                    self.cursor += 1
            else:
                raise ValueError('Unknown action')
            return self.state()

    def close(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--no-browser', action='store_true')
    args = parser.parse_args()
    if not (1 <= args.port <= 65535):
        parser.error('Port must be 1..65535')
    if not (ROOT / 'build' / 'exchange').exists():
        parser.error('Run bash scripts/build.sh first')
    session = Session()
    token = secrets.token_urlsafe(32)
    allowed_hosts = {f'127.0.0.1:{args.port}', f'localhost:{args.port}'}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def reply(self, data, status=200, mime='application/json'):
            body = json.dumps(data).encode() if mime == 'application/json' else data
            self.send_response(status)
            self.send_header('Content-Type', mime)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('X-Frame-Options', 'DENY')
            self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'; form-action 'self'")
            self.end_headers()
            self.wfile.write(body)

        def host_allowed(self):
            if self.headers.get('Host') not in allowed_hosts:
                self.reply({'error': 'Host not allowed'}, 403)
                return False
            return True

        def do_GET(self):
            if not self.host_allowed():
                return
            path = urlparse(self.path).path
            if path == '/api/state':
                with session.lock:
                    self.reply(dict(session.state(), token=token))
            elif path == '/api/export':
                with session.lock:
                    payload = '# ExchangeLab event journal; synthetic one-instrument session\n' + '\n'.join(session.journal) + '\n'
                    self.reply(payload.encode(), mime='text/plain; charset=utf-8')
            else:
                files = {'/': ('index.html', 'text/html; charset=utf-8'),
                         '/style.css': ('style.css', 'text/css; charset=utf-8'),
                         '/app.js': ('app.js', 'text/javascript; charset=utf-8')}
                if path not in files:
                    self.reply({'error': 'Not found'}, 404)
                    return
                filename, mime = files[path]
                self.reply((WEB / filename).read_bytes(), mime=mime)

        def do_POST(self):
            if not self.host_allowed():
                return
            origin = self.headers.get('Origin')
            if (origin and origin not in {f'http://{h}' for h in allowed_hosts}) or self.headers.get('X-Session-Token') != token:
                self.reply({'error': 'Invalid local session'}, 403)
                return
            if self.path != '/api/action' or self.headers.get('Content-Type', '').split(';')[0] != 'application/json':
                self.reply({'error': 'Use JSON at /api/action'}, 400)
                return
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 < length <= MAX_BODY:
                    raise ValueError('Request size outside allowed range')
                data = json.loads(self.rfile.read(length))
                if not isinstance(data, dict):
                    raise ValueError('Expected JSON object')
                self.reply(session.action(data))
            except (ValueError, UnicodeDecodeError) as exc:
                self.reply({'error': str(exc)}, 400)
            except Exception as exc:
                self.reply({'error': str(exc)}, 500)

    try:
        server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    except OSError as exc:
        session.close()
        raise SystemExit(f'Cannot bind local server: {exc}. For an occupied port, try: python3 server.py --port 8766')
    url = f'http://127.0.0.1:{args.port}'
    print(f'ExchangeLab: {url}\nLocal simulation only. Ctrl+C to stop.', flush=True)
    if not args.no_browser:
        threading.Timer(.4, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        session.close()

if __name__ == '__main__':
    main()
