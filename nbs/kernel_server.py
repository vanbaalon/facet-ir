#!/usr/bin/env python3
"""
Facet remote kernel server.

Run this on any machine with Python + SymPy installed, then add it in VS Code
with  Facet: Init Kernel → remote → http://<host>:8765

The server keeps SymPy loaded in memory and evaluates one expression per POST.

Usage:
    python3 kernel_server.py [--port 8765] [--host 0.0.0.0]

Free-tier hosting options (install Python + sympy, then run this script):
    • Oracle Cloud Free Tier  — always-free ARM VM
    • fly.io free tier        — `fly launch` with a Dockerfile
    • GitHub Codespaces       — forward port 8765 via VS Code port forwarding
    • Google Colab            — use ngrok or localtunnel to expose the port
"""

import argparse
import json
import re
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

import sympy as s

known = {
    'integrate': s.integrate, 'summation': s.summation, 'limit': s.limit,
    'Integral': s.Integral,   'Sum': s.Sum,             'Product': s.Product,
    'Limit': s.Limit,         'Lambda': s.Lambda,        'diff': s.diff,
    'sin': s.sin,   'cos': s.cos,   'tan': s.tan,       'log': s.log,
    'exp': s.exp,   'sqrt': s.sqrt, 'Abs': s.Abs,       'factor': s.factor,
    'simplify': s.simplify,   'expand': s.expand,
    'Eq': s.Eq, 'Ne': s.Ne,   'Gt': s.Gt, 'Ge': s.Ge,
    'Lt': s.Lt, 'Le': s.Le,   'pi': s.pi, 'oo': s.oo,  'E': s.E,
}
_srepr_ns = {k: getattr(s, k) for k in dir(s) if not k.startswith('_')}


def evaluate(src: str, session: dict) -> dict:
    env = dict(known)
    for k, v in session.items():
        try:
            env[k] = eval(v, {'__builtins__': {}}, _srepr_ns)
        except Exception:
            pass
    for name in set(re.findall(r'\b[A-Za-z_]\w*\b', src)):
        if name not in env:
            env[name] = s.Symbol(name)
    expr = eval(src, {'__builtins__': {}}, env)
    return {'ok': True, 'srepr': s.srepr(expr)}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        # Minimal logging: method + path + status only
        print(f'{self.command} {self.path} → {args[1]}', flush=True)

    def do_POST(self):
        length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(length)
        try:
            req = json.loads(body)
            result = evaluate(req['src'], req.get('session', {}))
        except Exception as e:
            result = {'ok': False, 'error': str(e)}

        payload = json.dumps(result).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        # Health-check endpoint
        payload = b'{"status":"ok"}'
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


def main():
    parser = argparse.ArgumentParser(description='Facet remote kernel server')
    parser.add_argument('--host', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()

    print(f'Facet kernel server — SymPy {s.__version__}', flush=True)
    print(f'Listening on http://{args.host}:{args.port}', flush=True)
    HTTPServer((args.host, args.port), Handler).serve_forever()


if __name__ == '__main__':
    main()
