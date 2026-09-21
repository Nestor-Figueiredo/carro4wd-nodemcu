/*
 * Carrinho 4WD — NodeMCU (ESP-12E) + Motor Shield L293D (HW-588A)
 * Projeto 156 "Auto motor 4WD" — Conhecimento 1037
 *
 * RECURSOS (v3):
 *  - Página com setas (segurar = anda) E JOYSTICK (arrasta o dedo) + slider de velocidade
 *  - TRIM (ajuste fino de lado que puxa)
 *  - Dead-man (para sozinho sem comando) + WATCHDOG de WiFi (para se perder a rede)
 *  - TELEMETRIA DA BATERIA (divisor no A0) -> aparece no /status e no monitor
 *  - OTA (atualização por WiFi, sem cabo!)
 *
 * API:  GET /cmd?d=f|t|e|d|s&v=0..255&trim=-40..40
 *       GET /status            (JSON: acao, velocidade, trim, bateria, ip, wifi)
 *       GET /roda?l=1|2        (gira um lado — calibração)
 *       GET /teste             (varredura dos 4 sentidos)
 *
 * HARDWARE: direito = D1(vel)/D3(dir) · esquerdo = D2(vel)/D4(dir)
 *           bateria -> divisor (2 resistores iguais) -> A0
 */
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include "config.h"

#ifndef PIN_DIR_VEL
#define PIN_DIR_VEL  5
#define PIN_DIR_DIR  0
#define PIN_ESQ_VEL  4
#define PIN_ESQ_DIR  2
#endif
#ifndef MOTOR_DIR_INVERTIDO
#define MOTOR_DIR_INVERTIDO 0
#define MOTOR_ESQ_INVERTIDO 1
#endif
#ifndef DEADMAN_MS
#define DEADMAN_MS 700
#endif
#ifndef VEL_PADRAO
#define VEL_PADRAO 200
#endif
#ifndef HOSTNAME
#define HOSTNAME "carro4wd"
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif
#ifndef BAT_DIVISOR
#define BAT_DIVISOR 2.0      // divisor: 2 resistores iguais => 2.0
#endif
#ifndef BAT_MIN
#define BAT_MIN 4.6          // abaixo disso = bateria fraca
#endif
#ifndef WIFI_WATCHDOG_MS
#define WIFI_WATCHDOG_MS 3000
#endif

ESP8266WebServer server(80);
int velocidade = VEL_PADRAO;
int trim = 0;                 // -40..40 (compensa lado que puxa)
char acao = 's';
unsigned long ultimoComando = 0;
unsigned long ultimaWifiOk = 0;
unsigned long ultimaAtividade = 0;   // ultima requisicao HTTP (mantem o WiFi acordado)
unsigned long contadorComandos = 0;   // contador REAL de comandos (o antigo "comandos" era millis()/1000 = uptime)
bool wifiRapido = true;              // false = em economia (modem sleep ligado)
float bateria = 0;
int wifiRssi = 0;

void motorFrente(int pinVel, int pinDir, int vel, int inv) {
  digitalWrite(pinDir, inv ? HIGH : LOW);
  analogWrite(pinVel, vel);
}
void motorTras(int pinVel, int pinDir, int vel, int inv) {
  digitalWrite(pinDir, inv ? LOW : HIGH);
  analogWrite(pinVel, vel);
}
void motorParar(int pinVel) { analogWrite(pinVel, 0); }

int comTrim(int v, int delta) {
  int r = v + delta;
  if (r < 0) r = 0;
  if (r > 255) r = 255;
  return r;
}

void aplicar(char a) {
  int vd = comTrim(velocidade, -trim);   // lado "direito" do codigo
  int ve = comTrim(velocidade, trim);    // lado "esquerdo" do codigo
  ultimaAtividade = millis();
  bool partindo = (acao == 's' && a != 's');
  if (partindo) {                        // kick-start: vence a inercia
    vd = ve = 255;
  }
  switch (a) {
    case 'f':
      motorFrente(PIN_DIR_VEL, PIN_DIR_DIR, vd, MOTOR_DIR_INVERTIDO);
      motorFrente(PIN_ESQ_VEL, PIN_ESQ_DIR, ve, MOTOR_ESQ_INVERTIDO);
      break;
    case 't':
      motorTras(PIN_DIR_VEL, PIN_DIR_DIR, vd, MOTOR_DIR_INVERTIDO);
      motorTras(PIN_ESQ_VEL, PIN_ESQ_DIR, ve, MOTOR_ESQ_INVERTIDO);
      break;
    case 'e':
      motorTras(PIN_DIR_VEL, PIN_DIR_DIR, vd, MOTOR_DIR_INVERTIDO);
      motorFrente(PIN_ESQ_VEL, PIN_ESQ_DIR, ve, MOTOR_ESQ_INVERTIDO);
      break;
    case 'd':
      motorFrente(PIN_DIR_VEL, PIN_DIR_DIR, vd, MOTOR_DIR_INVERTIDO);
      motorTras(PIN_ESQ_VEL, PIN_ESQ_DIR, ve, MOTOR_ESQ_INVERTIDO);
      break;
    default:
      motorParar(PIN_DIR_VEL);
      motorParar(PIN_ESQ_VEL);
      a = 's';
  }
  acao = a;
  ultimoComando = millis();
  if (partindo) delay(120);
}

void lerBateria() {
  long soma = 0;
  for (int i = 0; i < 10; i++) { soma += analogRead(A0); delay(2); }
  float v = (soma / 10.0) * (3.3 / 1023.0) * BAT_DIVISOR;
  bateria = v;
}

// ------------------------------------------------------------------ página
const char PAGINA[] PROGMEM = R"HTML(<!doctype html><html lang="pt-br"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Carrinho 4WD</title><style>
 body{background:#0d1117;color:#e6edf3;font-family:-apple-system,Segoe UI,Roboto,sans-serif;margin:0;padding:14px;text-align:center;-webkit-user-select:none;user-select:none}
 h1{font-size:1.05rem;margin:0 0 10px}
 .pad{position:relative;width:280px;height:280px;margin:0 auto;border-radius:50%;background:#161b22;border:1px solid #30363d;touch-action:none}
 .pad .knob{position:absolute;width:76px;height:76px;border-radius:50%;background:#1f6feb;left:102px;top:102px;pointer-events:none}
 .pad .rot{position:absolute;color:#8b949e;font-size:12px}
 .grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;max-width:320px;margin:14px auto}
 .grid button{height:66px;font-size:22px;border-radius:12px;border:1px solid #30363d;background:#161b22;color:#e6edf3}
 .grid button:active{background:#1f6feb}
 .stop{background:#3d1418 !important;border-color:#f85149 !important}
 .vazio{visibility:hidden}
 input[type=range]{width:100%}
 .cx{max-width:320px;margin:10px auto;text-align:left;font-size:13px;color:#8b949e}
 .st{color:#8b949e;font-size:.8rem;margin-top:8px}
</style></head><body>
<h1>🚗 Carrinho 4WD</h1>
<div id="camw" style="display:none;margin:0 auto 10px;max-width:320px"><img id="cam" alt="camera" style="width:100%;border-radius:12px;border:1px solid #30363d"></div>
<div style="margin-bottom:8px"><button id="bcam" onclick="cam()" style="padding:8px 14px;border-radius:8px;border:1px solid #30363d;background:#161b22;color:#e6edf3">📷 câmera</button></div>
<div class="pad" id="pad"><div class="knob" id="knob"></div></div>
<div class="grid">
  <button class="vazio"></button><button ontouchstart="ir('f',event)" onmousedown="ir('f',event)" ontouchend="parar(event)" onmouseup="parar(event)">▲</button><button class="vazio"></button>
  <button ontouchstart="ir('e',event)" onmousedown="ir('e',event)" ontouchend="parar(event)" onmouseup="parar(event)">◀</button>
  <button class="stop" onclick="cmd('d=s')">■</button>
  <button ontouchstart="ir('d',event)" onmousedown="ir('d',event)" ontouchend="parar(event)" onmouseup="parar(event)">▶</button>
  <button class="vazio"></button><button ontouchstart="ir('t',event)" onmousedown="ir('t',event)" ontouchend="parar(event)" onmouseup="parar(event)">▼</button><button class="vazio"></button>
</div>
<div class="cx">Velocidade: <b id="vv">__VEL__</b>
 <input type="range" min="0" max="255" value="__VEL__" id="sl" oninput="document.getElementById('vv').innerText=this.value" onchange="cmd('v='+this.value)">
 Trim (lado que puxa): <b id="tt">0</b>
 <input type="range" min="-40" max="40" value="0" id="tr" oninput="document.getElementById('tt').innerText=this.value" onchange="cmd('trim='+this.value)">
</div>
<div class="st" id="st">-</div>
<div style="color:#8b949e;font-size:12px;margin-top:6px">Teclado: <b>setas</b> ou <b>W A S D</b> = direção · <b>espaço</b> = parar</div>
<div style="margin-top:12px"><small>Calibração:</small><br>
 <button onclick="roda(1)" style="margin:4px;padding:8px 12px;border-radius:8px;border:1px solid #30363d;background:#161b22;color:#e6edf3">teste lado 1</button>
 <button onclick="roda(2)" style="margin:4px;padding:8px 12px;border-radius:8px;border:1px solid #30363d;background:#161b22;color:#e6edf3">teste lado 2</button>
</div>
<script>
var rep=null;
function cmd(q){fetch('/cmd?'+q).then(r=>r.text()).then(t=>document.getElementById('st').innerText=t).catch(e=>{})}
function ir(d,e){if(e)e.preventDefault();cmd('d='+d);if(rep)clearInterval(rep);rep=setInterval(function(){cmd('d='+d)},250)}
function parar(e){if(e)e.preventDefault();if(rep){clearInterval(rep);rep=null}cmd('d=s')}
function roda(l){fetch('/roda?l='+l).then(r=>r.text()).then(t=>document.getElementById('st').innerText=t).catch(e=>{})}
// ---- camera (polling /capture: mais robusto que MJPEG, nao trava a ESP32) ----
var camT=null;
function cam(){var w=document.getElementById('camw'),i=document.getElementById('cam'),b=document.getElementById('bcam');
 if(camT){clearInterval(camT);camT=null;i.removeAttribute('src');w.style.display='none';b.textContent='📷 câmera';}
 else{w.style.display='block';b.textContent='📷 câmera ON';
  var f=function(){i.src='http://192.168.1.162/capture?t='+Date.now();};f();camT=setInterval(f,300);}}
// ---- joystick ----
var pad=document.getElementById('pad'), knob=document.getElementById('knob');
var ativo=false, env=null, dir='s', vel=0;
function pos(e){var r=pad.getBoundingClientRect();var t=(e.touches?e.touches[0]:e);return {x:t.clientX-r.left-r.width/2, y:t.clientY-r.top-r.height/2};}
function envia(){cmd('d='+dir+'&v='+vel);}
function mover(e){
  if(!ativo) return; e.preventDefault();
  var p=pos(e), lim=102, d=Math.hypot(p.x,p.y);
  if(d>lim){p.x=p.x*lim/d; p.y=p.y*lim/d;}
  knob.style.left=(102+p.x)+'px'; knob.style.top=(102+p.y)+'px';
  var fx=p.x/lim, fy=p.y/lim;
  vel=Math.min(255, Math.round(Math.hypot(fx,fy)*255));
  if(vel<40){dir='s'; pararJoystick(); return;}
  dir=(Math.abs(fx)>Math.abs(fy)) ? (fx>0?'d':'e') : (fy>0?'t':'f');
  envia();
  if(!env) env=setInterval(envia,200);
}
function pararJoystick(){ if(env){clearInterval(env);env=null;} }
function soltar(){ativo=false; pararJoystick(); dir='s'; vel=0; knob.style.left='102px'; knob.style.top='102px'; cmd('d=s');}
pad.addEventListener('touchstart',function(e){ativo=true;mover(e)},{passive:false});
pad.addEventListener('touchmove',mover,{passive:false});
pad.addEventListener('touchend',soltar);
pad.addEventListener('mousedown',function(e){ativo=true;mover(e)});
window.addEventListener('mousemove',mover);
window.addEventListener('mouseup',soltar);
setInterval(function(){fetch('/status').then(r=>r.json()).then(j=>{
  document.getElementById('st').innerText='ação: '+j.acao+' · vel: '+j.velocidade+' · bateria: '+j.bateria+'V · IP: '+j.ip;
}).catch(e=>{});},1500)
// ---- teclado ----
var MAPA={'ArrowUp':'f','ArrowDown':'t','ArrowLeft':'e','ArrowRight':'d','w':'f','W':'f','s':'t','S':'t','a':'e','A':'e','d':'d','D':'d'};
var teclaAtiva=null;
function tecla(ev, pressionando){
  var k=ev.key;
  if(k===' '||k==='Spacebar'||k==='Escape'){ if(pressionando){ev.preventDefault(); teclaAtiva=null; parar();} return; }
  var acao=MAPA[k];
  if(!acao) return;
  ev.preventDefault();
  if(pressionando){ if(teclaAtiva!==acao){ teclaAtiva=acao; ir(acao); } }
  else if(teclaAtiva===acao){ teclaAtiva=null; parar(); }
}
document.addEventListener('keydown',function(e){tecla(e,true)});
document.addEventListener('keyup',function(e){tecla(e,false)});
</script></body></html>)HTML";

void rotaPagina() { server.send_P(200, "text/html", PAGINA); }

void rotaCmd() {
  ultimaAtividade = millis();
  contadorComandos++;
  if (server.hasArg("v")) {
    int v = server.arg("v").toInt();
    velocidade = (v < 0) ? 0 : (v > 255 ? 255 : v);
  }
  if (server.hasArg("trim")) {
    int t = server.arg("trim").toInt();
    trim = (t < -40) ? -40 : (t > 40 ? 40 : t);
  }
  if (server.hasArg("d")) aplicar(server.arg("d").charAt(0));
  else if (server.hasArg("v") || server.hasArg("trim")) { if (acao != 's') aplicar(acao); }
  String r = "acao=" + String(acao) + " velocidade=" + String(velocidade) + " trim=" + String(trim)
           + " bateria=" + String(bateria, 2) + "V ip=" + WiFi.localIP().toString();
  server.send(200, "text/plain", r);
}

void rotaStatus() {
  ultimaAtividade = millis();
  String j = "{\"acao\":\"" + String(acao) + "\",\"velocidade\":" + String(velocidade)
           + ",\"trim\":" + String(trim) + ",\"bateria\":" + String(bateria, 2)
           + ",\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false")
           + ",\"rssi\":" + String(WiFi.RSSI())
           + ",\"energia\":" + String(wifiRapido ? "rapido" : "economia")
           + ",\"ip\":\"" + WiFi.localIP().toString() + "\""
           + ",\"uptime\":" + String(millis() / 1000)
           + ",\"reset\":\"" + ESP.getResetReason() + "\""
           + ",\"resetcod\":" + String(ESP.getResetInfoPtr()->reason)
           + ",\"exccause\":" + String(ESP.getResetInfoPtr()->exccause)
           + ",\"heap\":" + String(ESP.getFreeHeap())
           + ",\"comandos\":" + String(contadorComandos) + "}";
  server.send(200, "application/json", j);
}

void rotaRoda() {
  String l = server.arg("l");
  if (l == "1") motorFrente(PIN_DIR_VEL, PIN_DIR_DIR, 220, MOTOR_DIR_INVERTIDO);
  else motorFrente(PIN_ESQ_VEL, PIN_ESQ_DIR, 220, MOTOR_ESQ_INVERTIDO);
  delay(1500);
  motorParar(PIN_DIR_VEL); motorParar(PIN_ESQ_VEL);
  acao = 's'; ultimoComando = millis();
  server.send(200, "text/plain", "lado " + l + " girou pra frente por 1,5 s");
}

void rotaTeste() {
  Serial.println(F("[teste] varredura"));
  aplicar('f'); delay(1200); aplicar('t'); delay(1200);
  aplicar('e'); delay(1200); aplicar('d'); delay(1200); aplicar('s');
  server.send(200, "text/plain", "teste ok (veja o serial)");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("=== Carrinho 4WD v3 (OTA + bateria + joystick) ==="));

  pinMode(PIN_DIR_VEL, OUTPUT); pinMode(PIN_DIR_DIR, OUTPUT);
  pinMode(PIN_ESQ_VEL, OUTPUT); pinMode(PIN_ESQ_DIR, OUTPUT);
  analogWriteRange(255);
  analogWriteFreq(1000);
  aplicar('s');
  lerBateria();

  WiFi.hostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);   // acorda no boot; o loop alterna conforme o uso
  Serial.print(F("MAC: ")); Serial.println(WiFi.macAddress());
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(300); Serial.print('.'); }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(); Serial.print(F("IP: ")); Serial.println(WiFi.localIP());
    ultimaWifiOk = millis();
  } else {
    Serial.println(F("\nWiFi de casa nao respondeu -> AP"));
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print(F("AP ")); Serial.print(AP_SSID); Serial.print(F(" IP: ")); Serial.println(WiFi.softAPIP());
  }

  // ---- OTA: atualização por WiFi ----
  ArduinoOTA.setHostname(HOSTNAME);
  if (String(OTA_PASSWORD).length() > 0) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { aplicar('s'); Serial.println(F("[OTA] recebendo firmware...")); });
  ArduinoOTA.onEnd([]() { Serial.println(F("[OTA] ok! reiniciando")); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.print(F("[OTA] erro ")); Serial.println(e); });
  ArduinoOTA.begin();
  Serial.print(F("OTA pronto — IP "));
  Serial.println(WiFi.localIP());

  server.on("/", rotaPagina);
  server.on("/cmd", rotaCmd);
  server.on("/status", rotaStatus);
  server.on("/roda", rotaRoda);
  server.on("/teste", rotaTeste);
  server.onNotFound(rotaPagina);
  server.begin();
  Serial.println(F("pagina no ar"));
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();

  // bateria: le de vez em quando
  static unsigned long ultimaLeitura = 0;
  if (millis() - ultimaLeitura > 10000) { ultimaLeitura = millis(); lerBateria(); }

  // WiFi adaptativo: acordado enquanto alguem usa (pagina aberta) e em economia quando ocioso
  bool ativo = (acao != 's') || (millis() - ultimaAtividade < 8000);
  if (ativo && !wifiRapido) {
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    wifiRapido = true;
    Serial.println(F("[wifi] modo rapido (em uso)"));
  } else if (!ativo && wifiRapido) {
    WiFi.setSleepMode(WIFI_MODEM_SLEEP);
    wifiRapido = false;
    Serial.println(F("[wifi] economia (parado)"));
  }

  // watchdog de WiFi: perdeu a rede => para
  if (WiFi.status() == WL_CONNECTED) {
    ultimaWifiOk = millis();
  } else if (acao != 's' && millis() - ultimaWifiOk > WIFI_WATCHDOG_MS) {
    aplicar('s');
    Serial.println(F("[watchdog] WiFi caiu — parei"));
  }
  static unsigned long ultimaTentativa = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - ultimaTentativa > 15000) {
    ultimaTentativa = millis();
    WiFi.reconnect();
  }

  // dead man
  if (acao != 's' && millis() - ultimoComando > DEADMAN_MS) {
    aplicar('s');
    Serial.println(F("[deadman] parado por falta de comando"));
  }
}
