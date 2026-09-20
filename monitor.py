#!/usr/bin/env python3
"""Monitor do carrinho 4WD (NodeMCU + shield L293D).

Consulta o /status do carrinho (http://carro4wd/status) a cada intervalo, registra
mudancas de estado (online/offline, acao, velocidade) e serve uma pagina:
  GET /      -> pagina com o estado atual + historico
  GET /raw   -> o log em texto
"""
import datetime
import html
import json
import os
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CARRO = os.environ.get('CARRO_URL', 'http://carro4wd/status')
LOG = os.environ.get('CARRO_LOG', '/home/pi/carro4wd/monitor.log')
PORTA = int(os.environ.get('CARRO_WEB', '8097'))
INTERVALO = int(os.environ.get('CARRO_INTERVALO', '30'))

_lock = threading.Lock()
_estado = {'online': None, 'acao': '-', 'velocidade': '-', 'desde': '-', 'eventos': []}


def _grava(texto):
    with _lock:
        _estado['eventos'].insert(0, texto)
        del _estado['eventos'][200:]
    try:
        with open(LOG, 'a') as f:
            f.write(texto + '\n')
    except Exception:
        pass


def carrega_historico():
    try:
        with open(LOG) as f:
            linhas = [l.rstrip('\n') for l in f if l.strip()]
        with _lock:
            _estado['eventos'] = linhas[-200:][::-1]
    except FileNotFoundError:
        pass


def poll():
    anterior = None
    while True:
        agora = datetime.datetime.now().strftime('%d/%m %H:%M:%S')
        try:
            req = urllib.request.Request(CARRO)
            dados = json.loads(urllib.request.urlopen(req, timeout=6).read().decode())
            atual = ('on', dados.get('acao'), str(dados.get('velocidade')))
        except Exception:
            dados, atual = None, ('off', '-', '-')
        with _lock:
            online = atual[0] == 'on'
            mudou_online = _estado['online'] != online
            mudou_dados = (atual[1], atual[2]) != (_estado['acao'], _estado['velocidade'])
            _estado['online'] = online
            if online:
                _estado['acao'], _estado['velocidade'] = atual[1], atual[2]
        if mudou_online:
            _grava('%s  %s' % (agora, 'CARRINHO ONLINE' if atual[0] == 'on' else 'carrinho OFFLINE'))
            with _lock:
                _estado['desde'] = agora
        elif mudou_dados and atual[0] == 'on':
            _grava('%s  acao=%s velocidade=%s' % (agora, atual[1], atual[2]))
        time.sleep(INTERVALO)


def pagina():
    with _lock:
        online = _estado['online']
        acao, vel, desde = _estado['acao'], _estado['velocidade'], _estado['desde']
        eventos = list(_estado['eventos'])
    cor = '#3fb950' if online else '#f85149'
    situacao = 'ONLINE' if online else ('OFFLINE' if online is False else 'verificando...')
    linhas = []
    for e in eventos:
        cls = 'err' if 'OFFLINE' in e else 'ok'
        linhas.append('<tr class="%s"><td>%s</td></tr>' % (cls, html.escape(e)))
    return """<!doctype html><html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><meta http-equiv="refresh" content="15">
<title>Carrinho 4WD — Monitor</title><style>
 body{background:#0d1117;color:#e6edf3;font-family:-apple-system,Segoe UI,Roboto,sans-serif;margin:0;padding:18px}
 h1{font-size:1.05rem;margin:0 0 12px}
 .cx{border:1px solid #21262d;border-radius:12px;padding:14px;margin-bottom:16px;background:#161b22}
 .big{font-size:1.5rem;font-weight:700;color:%s}
 .sub{color:#8b949e;font-size:.82rem;margin-top:4px}
 table{width:100%%;border-collapse:collapse;font-size:.85rem}
 td{padding:5px 8px;border-bottom:1px solid #21262d}
 tr.err td{color:#f85149} tr.ok td{color:#3fb950}
 a.bt{display:inline-block;margin-top:10px;background:#1f6feb;color:#fff;padding:8px 14px;border-radius:8px;text-decoration:none;font-weight:600}
</style></head><body>
<h1>🚗 Carrinho 4WD — Monitor</h1>
<div class="cx">
  <div class="big">%s</div>
  <div class="sub">ação: <b>%s</b> · velocidade: <b>%s</b> · neste estado desde: %s</div>
  <a class="bt" href="http://192.168.1.160/" target="_blank">Abrir a página do carrinho</a>
</div>
<table>%s</table>
</body></html>""" % (cor, situacao, html.escape(str(acao)), html.escape(str(vel)), html.escape(str(desde)),
                     '\n'.join(linhas) or '<tr><td>sem eventos ainda</td></tr>')


class H(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith('/raw'):
            try:
                dados = open(LOG, 'rb').read()[-100000:]
            except Exception:
                dados = b'(sem log)'
            tipo = 'text/plain; charset=utf-8'
        else:
            dados = pagina().encode('utf-8')
            tipo = 'text/html; charset=utf-8'
        self.send_response(200)
        self.send_header('Content-Type', tipo)
        self.send_header('Content-Length', str(len(dados)))
        self.end_headers()
        self.wfile.write(dados)

    def log_message(self, *a):
        pass


if __name__ == '__main__':
    carrega_historico()
    threading.Thread(target=poll, daemon=True).start()
    print('carro4wd-monitor na porta %d | alvo %s | log %s' % (PORTA, CARRO, LOG), flush=True)
    ThreadingHTTPServer(('0.0.0.0', PORTA), H).serve_forever()
