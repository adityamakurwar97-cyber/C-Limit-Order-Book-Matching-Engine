#!/usr/bin/env python3
"""Runs real HTTP requests against the local bridge and C++ subprocess."""
import json
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def require(ok, message):
    if not ok:
        raise AssertionError(message)

def main():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    process = subprocess.Popen([sys.executable, str(ROOT / 'server.py'), '--no-browser', '--port', str(port)],
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    url = f'http://127.0.0.1:{port}'
    try:
        initial = None
        for _ in range(100):
            try:
                with urllib.request.urlopen(url + '/api/state', timeout=1) as r:
                    initial = json.load(r)
                break
            except (urllib.error.URLError, ConnectionError):
                time.sleep(.05)
        require(initial is not None, 'Server failed to start')
        token = initial['token']
        def action(data):
            req = urllib.request.Request(url + '/api/action', data=json.dumps(data).encode(),
                headers={'Content-Type': 'application/json', 'X-Session-Token': token})
            with urllib.request.urlopen(req, timeout=100) as r:
                return json.load(r)
        with urllib.request.urlopen(url) as r:
            require(b'EXCHANGE' in r.read(), 'HTML page missing')
        for path in ['/app.js', '/style.css']:
            with urllib.request.urlopen(url + path) as r:
                require(r.status == 200, 'Asset missing')
        state = action({'action': 'seed'})
        require(state['book']['active_count'] == 16, 'Seed liquidity')
        state = action({'action': 'command', 'command': 'BUY 1001 10004 260 IOC'})
        require(state['filled'] == 260 and len(state['new_trades']) == 3, 'Sweep through bridge')
        state = action({'action': 'command', 'command': 'CANCEL 101'})
        require(state['cancelled'] == 120, 'Cancellation through bridge')
        state = action({'action': 'command', 'command': 'BUY -1 10 1 GTC'})
        require(not state['ok'], 'Invalid ID rejected')
        final_book = state['book']
        with urllib.request.urlopen(url + '/api/export') as r:
            journal = r.read().decode()
        state = action({'action': 'load_replay', 'text': journal})
        for _ in range(state['replay_count']):
            state = action({'action': 'step'})
        require(state['book'] == final_book, 'Export/import replay differs')
        for scenario in ['sweep', 'fifo', 'fok']:
            state = action({'action': 'scenario', 'scenario': scenario})
            require(state['book']['trade_count'] > 0, 'Scenario has no trades')
        state = action({'action': 'benchmark'})
        results = state['benchmark']['results']
        require(len(results) == 2 and results[0]['checksum'] == results[1]['checksum'], 'Benchmark checksum mismatch')
        req = urllib.request.Request(url + '/api/action', data=b'{"action":"reset"}',
                                     headers={'Content-Type': 'application/json'})
        try:
            urllib.request.urlopen(req)
            raise AssertionError('Missing token accepted')
        except urllib.error.HTTPError as e:
            require(e.code == 403, 'Wrong CSRF rejection')
        print('PASS HTTP assets, validation, sweep, cancellation, three scenarios, exact journal replay, benchmark and CSRF checks')
    finally:
        # Server owns a subprocess; graceful SIGINT runs its cleanup.
        import signal
        process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()

if __name__ == '__main__':
    main()
