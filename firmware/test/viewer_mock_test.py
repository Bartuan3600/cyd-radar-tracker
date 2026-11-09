#!/usr/bin/env python3
"""Test des PC-Viewers (viewer/index.html) ohne Hardware: Mock-Web-Serial liefert echte JSON-Schnappschuesse aus
dem Host-Test (test/run_json_test.sh) plus Boot-Muell, kaputte Zeile und einen Frontfilter-Fall (Ziel hinten, f:0).
Headless Chrome fuehrt die Seite aus (Verbinden, Simulation, Reset, Baudrate) und macht einen Screenshot.
Aufruf: viewer_mock_test.py [ausgabe_png]   (braucht google-chrome, python3, /tmp/cyd_json)"""
import time, math, json, glob, os, shutil, subprocess, sys, threading, http.server, functools, socketserver, tempfile, re
HERE = os.path.dirname(os.path.abspath(__file__)); V = os.path.join(HERE, '..', 'viewer')
PNG = sys.argv[1] if len(sys.argv) > 1 else '/tmp/cyd-viewer-mock.png'
PNG_TRAIL = PNG.replace('.png', '-trail.png')
D = tempfile.mkdtemp(prefix='cydviewer_'); shutil.copytree(V, D + '/site')
files = sorted(glob.glob('/tmp/cyd_json/state_*.json'))
assert files, 'erst test/run_json_test.sh ausfuehren'
st = {int(f[-10:-5]): json.load(open(f)) for f in files}
back = json.loads(json.dumps(st[16000]))
back['r450'][2] = {"v": 1, "x": 100, "y": -800, "sp": -30, "res": 360, "f": 0}; back['fd'] = 4; back['t'] = 16125
lines = [json.dumps(st[t], separators=(',', ':')) for t in sorted(st) if t <= 16000] + [json.dumps(back, separators=(',', ':'))]
data = ('ets Jun  8 2016 00:22:57\r\nrst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)\r\n# CYD-Tracker-2sensor front y>150 r>=300 fov60 drop0\n{kaputt\n'
        + '\n'.join(lines) + '\n')
mock = '''window.__written=[];
(function(){ var data=%s, bytes=new TextEncoder().encode(data), pos=0;
 var port={readable:null,writable:null,open:async function(o){window.__open=o;
   port.readable=new ReadableStream({pull:function(c){ if(pos>=bytes.length){return new Promise(function(){})} var n=Math.min(97,bytes.length-pos); c.enqueue(bytes.slice(pos,pos+n)); pos+=n; return new Promise(function(r){setTimeout(r,2)}) }});
   port.writable=new WritableStream({write:function(ch){window.__written.push(new TextDecoder().decode(ch))}}) },
  close:async function(){}, setSignals:async function(s){window.__sig=s}};
 Object.defineProperty(navigator,'serial',{configurable:true,value:{requestPort:async function(){return port},getPorts:async function(){return [port]},addEventListener:function(){}}});
})();
''' % json.dumps(data)
# Zweiter Mock: genau 1 JSON-Zeile alle 50 ms (20 Hz wie die Firmware), um Zeilen/s, Zeichnungen und Latenz im Viewer zu messen
paced_lines = [json.dumps(st[t], separators=(',', ':')) for t in sorted(st) if 3000 <= t <= 8500]
paced = '''window.__written=[];
(function(){ var L=%s, i=0, pend=null, enc=new TextEncoder();
 var port={readable:null,writable:null,open:async function(o){ window.__open=o;
   port.readable=new ReadableStream({pull:function(c){ return new Promise(function(r){ setTimeout(function(){ c.enqueue(enc.encode(L[i%%L.length]+'\\n')); i++; r() },50) }) }});
   port.writable=new WritableStream({write:function(ch){window.__written.push(new TextDecoder().decode(ch))}}) },
  close:async function(){}, setSignals:async function(s){}};
 Object.defineProperty(navigator,'serial',{configurable:true,value:{requestPort:async function(){return port},getPorts:async function(){return [port]},addEventListener:function(){}}});
})();
''' % json.dumps(paced_lines)
base = open(D + '/site/index.html', encoding='utf-8').read()
def page(name, pre, auto, m=None):
    h = base.replace('<script>', '<script>' + pre + (m or mock) + '</script>\n<script>', 1).replace('</body>', '<script>' + auto + '</script></body>')
    open(D + '/site/' + name, 'w', encoding='utf-8').write(h)
RES = '''function sl(ms){return new Promise(function(r){setTimeout(r,ms)})}
function put(o){var e=document.createElement('pre');e.id='result';e.textContent=JSON.stringify(o);document.body.appendChild(e)}
var $=function(i){return document.getElementById(i)};'''
page('t1.html', '', '(async function(){' + RES + '''
 $('bc').click(); await sl(1200);
 var before={sum:$('sum').textContent, st:$('st').textContent, tt:$('tt').innerText, t0:$('t0').innerText, fr:$('fr').textContent,
   c2:$('c2').style.display, s420:$('s420').style.display, chips:[$('c0').className,$('c1').className]};
 $('bs').click(); $('br').click(); await sl(100);
 put({before:before, written:window.__written, open:window.__open, sig:window.__sig, err:$('err').style.display});})();''')
page('t2.html', "localStorage.setItem('baud','115200');", '(async function(){' + RES + '''
 $('bc').click(); await sl(500); put({open:window.__open, sel:$('baud').value});})();''')
page('shot.html', '', '(async function(){' + RES + " $('bc').click(); await sl(3000); })();")
page('t3.html', '', '(async function(){' + RES + '''
 var s0={off:$('bts').textContent, cls:$('bt').className, ls:localStorage.getItem('trail')};
 $('bc').click(); await sl(1500); var st=window.__stats; var n1=st.lines, tr0=st.trail;
 $('bt').click(); await sl(1500); var on={txt:$('bts').textContent, cls:$('bt').className, ls:localStorage.getItem('trail')};
 await sl(1500); var S=window.__stats; var el=(S.lastT-S.firstT)/1000;
 put({s0:s0, on:on, lines:S.lines, draws:S.draws, lps:(S.lines-1)/el, latAvg:S.latAvg, latMax:S.latMax, trail:S.trail, status:$('st').textContent, pf:$('pf').textContent, el:el});})();''', paced)
page('t4.html', '', '(async function(){' + RES + '''
 $('bc').click(); await sl(4000); var S=window.__stats; var el=(S.lastT-S.firstT)/1000;
 var u=encodeURIComponent(JSON.stringify({lines:S.lines, draws:S.draws, timer:S.timerDraws, lps:(S.lines-1)/el, latAvg:S.latAvg, latMax:S.latMax, fps:S.fps, el:el}));
 fetch('/report?'+u);})();''', paced)
page('shot2.html', "localStorage.setItem('trail','1');", '(async function(){' + RES + " $('bc').click(); await sl(3000); })();", paced)
REPORT = {}
class Q(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        if self.path.startswith('/report?'):
            import urllib.parse; REPORT.update(json.loads(urllib.parse.unquote(self.path[8:]))); self.send_response(204); self.end_headers(); return
        super().do_GET()
socketserver.TCPServer.allow_reuse_address = True
srv = socketserver.TCPServer(('127.0.0.1', 0), functools.partial(Q, directory=D + '/site')); port = srv.server_address[1]
threading.Thread(target=srv.serve_forever, daemon=True).start()
def chrome(url, *extra):
    return subprocess.run(['google-chrome', '--headless=new', '--no-sandbox', '--disable-gpu', '--hide-scrollbars'] + list(extra) + [url],
                          capture_output=True, text=True, timeout=120)
def result(name):
    out = chrome(f'http://127.0.0.1:{port}/{name}', '--virtual-time-budget=8000', '--dump-dom').stdout
    m = re.search(r'<pre id="result">(.*?)</pre>', out, re.S); assert m, 'kein Ergebnis in ' + name
    import html; return json.loads(html.unescape(m.group(1)))
fails = 0
def chk(c, msg):
    global fails
    print(('  ok   ' if c else '  FAIL ') + msg); fails += 0 if c else 1
r = result('t1.html')
b = r['before']
chk(r['open'] == {'baudRate': 460800, 'bufferSize': 8192}, 'Standard-Baudrate 460800: %s' % r['open'])
chk(r['sig'] == {'dataTerminalReady': False, 'requestToSend': False}, 'DTR/RTS aus (kein Reset/Bootmodus): %s' % r['sig'])
chk(b['st'].startswith('Live'), 'Status Live trotz Boot-Muell und kaputter Zeile: ' + b['st'])
chk('verworfen' in b['st'], 'verworfene Zeilen gezaehlt (Muell/kaputt)')
chk(b['c2'] == 'none' and b['s420'] == 'none', 'kein dritter Sensor (LD2420) sichtbar')
chk(b['chips'] == ['chip ok', 'chip ok'], 'beide Sensorchips gruen: %s' % b['chips'])
chk(b['sum'].startswith('Gegner vor dem Ger') and ': 2' in b['sum'], 'Summe: ' + b['sum'])
chk('verworfen (nicht vor dem Ger' in b['t0'], 'Ziel hinter dem Geraet (f:0) nur als "verworfen" in der Tabelle')
chk('-800' in b['t0'] and '-800' not in b['tt'], 'y=-800 nicht in der Gegnertabelle')
chk('Verworfen: 4' in b['fr'], 'Frontfilter-Zaehler: ' + b['fr'])
chk(r['written'] == ['sim 0\n', 'reset\n'], 'Befehle an die Firmware (Daten zeigen sim=1, also Umschalten auf sim 0): %r' % r['written'])
chk(r['err'] == 'none', 'keine Fehlermeldung')
r2 = result('t2.html')
chk(r2['open']['baudRate'] == 115200 and r2['sel'] == '115200', 'Baudrate waehlbar (115200 gespeichert): %s' % r2)
# --- neu: Messbarkeit (Zeilen/s, Zeichnungen, Latenz), Spur, Ganz-cm-Anzeige, res/mm in der Tabelle ---
r3 = result('t3.html')
chk(r3['s0']['off'] == 'aus' and r3['s0']['ls'] is None, 'Spur standardmaessig AUS: %s' % r3['s0'])
chk(r3['on']['txt'] == 'an' and r3['on']['cls'] == 'on' and r3['on']['ls'] == '1', 'Spur umschaltbar und gespeichert: %s' % r3['on'])
chk(18.0 <= r3['lps'] <= 22.0, 'Viewer verarbeitet %.1f Zeilen/s (Mock sendet 20/s, %d Zeilen in %.1f s)' % (r3['lps'], r3['lines'], r3['el']))
chk(r3['draws'] >= r3['lines'] * 0.4, 'virtuelle Zeit (rAF steht dort still, nur 60-ms-Timer-Sicherung aktiv; gewertet wird der Echtzeit-Lauf unten): %d Zeichnungen fuer %d Zeilen' % (r3['draws'], r3['lines']))
chk(r3['latMax'] <= 70, 'virtuelle Zeit: Latenz mittel %.1f ms, max %.1f ms (Limit 70 ms mit Timer-Sicherung)' % (r3['latAvg'], r3['latMax']))
chk(0 < r3['trail'] <= 25, 'Spur haelt nur ~1 s echte Messpunkte: %d Eintraege' % r3['trail'])
chk('Zeilen/s' in r3['status'] and 'Bilder/s' in r3['status'], 'Statuszeile zeigt Zeilen/s: ' + r3['status'])
chk('Glättung α=1.00 (aus: jede Messung exakt)' in r3['pf'] and 'Sprung > 1500 mm erst nach 2 Frames' in r3['pf'] and 'Anzeige am Gerät 25.2 fps' in r3['pf'], 'Konfigurations-/Geraetezeile: ' + r3['pf'])
def cmv(v): return str(int(math.floor(v / 10 + 0.5)))
fr = [t for t in back['tr'] if t['y'] > 0]
chk(all(cmv(t['x']) in b['tt'] and cmv(t['y']) in b['tt'] for t in fr) and 'X cm' in b['tt'] and 'Y cm' in b['tt'], 'Fusionstabelle zeigt Position in ganzen cm: %r' % b['tt'][:90])
chk('X mm' in b['t0'] and 'Y mm' in b['t0'] and 'Res mm' in b['t0'] and '360' in b['t0'], 'LD2450-Tabelle zeigt Rohkoordinaten in mm und Aufloesung (res)')
# Echtzeit-Lauf (ohne virtuelle Zeit): hier feuert requestAnimationFrame wirklich
pr = subprocess.Popen(['google-chrome', '--headless=new', '--no-sandbox', '--disable-gpu', f'http://127.0.0.1:{port}/t4.html'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(240):
    if REPORT: break
    time.sleep(0.1)
pr.kill(); pr.wait()
chk(bool(REPORT), 'Echtzeit-Lauf lieferte Messwerte')
if REPORT:
    R4 = REPORT
    chk(18.0 <= R4['lps'] <= 22.0, 'Echtzeit: %.1f Zeilen/s (%d Zeilen in %.1f s)' % (R4['lps'], R4['lines'], R4['el']))
    chk(R4['draws'] >= R4['lines'] - 1, 'Echtzeit: %d Zeichnungen fuer %d Zeilen (davon %d per Timer-Sicherung, Rest requestAnimationFrame)' % (R4['draws'], R4['lines'], R4['timer']))
    chk(R4['latMax'] <= 40, 'Echtzeit: Latenz Zeile -> Zeichnung mittel %.1f ms, max %.1f ms (Limit 40 ms)' % (R4['latAvg'], R4['latMax']))
    chk(R4['timer'] <= R4['draws'] * 0.1, 'Echtzeit: Zeichnung laeuft ueber requestAnimationFrame (Timer-Sicherung selten: %d)' % R4['timer'])
chrome(f'http://127.0.0.1:{port}/shot2.html', '--virtual-time-budget=3600', '--window-size=480,1500', '--screenshot=' + PNG_TRAIL)
chk(os.path.getsize(PNG_TRAIL) > 5000, 'Screenshot mit Spur ' + PNG_TRAIL)
chrome(f'http://127.0.0.1:{port}/shot.html', '--virtual-time-budget=1800', '--window-size=480,1500', '--screenshot=' + PNG)
chk(os.path.getsize(PNG) > 5000, 'Screenshot ' + PNG)
srv.shutdown(); shutil.rmtree(D, ignore_errors=True)
print('\nViewer-Test: %s' % ('ALLES OK' if not fails else '%d FEHLER' % fails)); sys.exit(1 if fails else 0)
