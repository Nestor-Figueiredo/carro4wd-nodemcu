/*
 * Carrinho 4WD — NodeMCU (ESP-12E) + Motor Shield L293D (HW-588A / "ESP Motor Shield")
 * Projeto 156 "Auto motor 4WD" — Conhecimento 1037
 *
 * CONTROLE: o carrinho entra no WiFi de casa e serve uma PÁGINA com setas +
 * slider de velocidade. Também tem API HTTP simples (para integrar depois):
 *     GET /cmd?d=f|t|e|d|s&v=0..255     (frente/trás/esquerda/direita/parar + velocidade)
 *     GET /status                       (JSON: ip, velocidade, última ação)
 *
 * HARDWARE (shield HW-588A — 2 motores: os 2 da direita juntos e os 2 da esquerda juntos):
 *     Motor DIREITO : velocidade = D1 (GPIO5),  direção = D3 (GPIO0)
 *     Motor ESQUERDO: velocidade = D2 (GPIO4),  direção = D4 (GPIO2)
 *     Alimentação dos motores: 4 pilhas AA no conector do shield (GND comum com o NodeMCU).
 *     (Se a placa não for a V2 do NodeMCU, a shield não encaixa.)
 *
 * SEGURANÇA: "dead man" — se o celular parar de mandar comando por 700 ms, o carro PARA.
 *
 * Config (WiFi etc.) em config.h (fora do git; use config.h.example).
 */
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "config.h"

#ifndef PIN_DIR_VEL
#define PIN_DIR_VEL  5     // D1 -> velocidade do motor direito (PWM)
#define PIN_DIR_DIR  0     // D3 -> direção do motor direito
#define PIN_ESQ_VEL  4     // D2 -> velocidade do motor esquerdo (PWM)
#define PIN_ESQ_DIR  2     // D4 -> direção do motor esquerdo
#endif
#ifndef MOTOR_DIR_INVERTIDO
#define MOTOR_DIR_INVERTIDO 0
#define MOTOR_ESQ_INVERTIDO 0
#endif
#ifndef DEADMAN_MS
#define DEADMAN_MS 700
#endif
#ifndef VEL_PADRAO
#define VEL_PADRAO 200
#endif
#ifndef HOSTNAME
#define HOSTNAME "carro4wd"      // nome que aparece no DHCP/DNS da casa
#endif

ESP8266WebServer server(80);
int velocidade = VEL_PADRAO;
char acao = 's';
unsigned long ultimoComando = 0;
unsigned long contador = 0;
String ultimaOrigem = "-";

// ---------------------------------------------------------------- motores
void motorFrente(int pinVel, int pinDir, int vel, int inv) {
  digitalWrite(pinDir, inv ? HIGH : LOW);
  analogWrite(pinVel, vel);
}
void motorTras(int pinVel, int pinDir, int vel, int inv) {
  digitalWrite(pinDir, inv ? LOW : HIGH);
  analogWrite(pinVel, vel);
}
void motorParar(int pinVel) { analogWrite(pinVel, 0); }

void aplicar(char a) {
  int v = velocidade;
  switch (a) {
    case 'f':   // frente
      motorFrente(PIN_DIR_VEL, PIN_DIR_DIR, v, MOTOR_DIR_INVERTIDO);
      motorFrente(PIN_ESQ_VEL, PIN_ESQ_DIR, v, MOTOR_ESQ_INVERTIDO);
      break;
    case 't':   // trás
      motorTras(PIN_DIR_VEL, PIN_DIR_DIR, v, MOTOR_DIR_INVERTIDO);
      motorTras(PIN_ESQ_VEL, PIN_ESQ_DIR, v, MOTOR_ESQ_INVERTIDO);
      break;
    case 'e':   // esquerda (gira: esquerda para trás, direita para frente)
      motorTras(PIN_ESQ_VEL, PIN_ESQ_DIR, v, MOTOR_ESQ_INVERTIDO);
      motorFrente(PIN_DIR_VEL, PIN_DIR_DIR, v, MOTOR_DIR_INVERTIDO);
      break;
    case 'd':   // direita
      motorFrente(PIN_ESQ_VEL, PIN_ESQ_DIR, v, MOTOR_ESQ_INVERTIDO);
      motorTras(PIN_DIR_VEL, PIN_DIR_DIR, v, MOTOR_DIR_INVERTIDO);
      break;
    default:    // parar
      motorParar(PIN_DIR_VEL);
      motorParar(PIN_ESQ_VEL);
      a = 's';
  }
  acao = a;
  ultimoComando = millis();
}

// ---------------------------------------------------------------- página
const char PAGINA[] PROGMEM = R"HTML(<!doctype html><html lang="pt-br"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Carrinho 4WD</title><style>
 body{background:#0d1117;color:#e6edf3;font-family:-apple-system,Segoe UI,Roboto,sans-serif;margin:0;padding:14px;text-align:center;-webkit-user-select:none;user-select:none}
 h1{font-size:1.1rem;margin:0 0 10px}
 .grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px;max-width:340px;margin:0 auto}
 .grid button{height:84px;font-size:26px;border-radius:14px;border:1px solid #30363d;background:#161b22;color:#e6edf3}
 .grid button:active{background:#1f6feb}
 .stop{background:#3d1418 !important;border-color:#f85149 !important}
 .vazio{visibility:hidden}
 .vel{margin:16px auto;max-width:340px;text-align:left}
 input[type=range]{width:100%}
 .st{color:#8b949e;font-size:.8rem;margin-top:10px}
</style></head><body>
<h1>🚗 Carrinho 4WD</h1>
<div class="grid">
  <button class="vazio"></button><button ontouchstart="ir('f',event)" onmousedown="ir('f',event)" ontouchend="parar(event)" onmouseup="parar(event)">▲</button><button class="vazio"></button>
  <button ontouchstart="ir('e',event)" onmousedown="ir('e',event)" ontouchend="parar(event)" onmouseup="parar(event)">◀</button>
  <button class="stop" onclick="cmd('d=s')">■</button>
  <button ontouchstart="ir('d',event)" onmousedown="ir('d',event)" ontouchend="parar(event)" onmouseup="parar(event)">▶</button>
  <button class="vazio"></button><button ontouchstart="ir('t',event)" onmousedown="ir('t',event)" ontouchend="parar(event)" onmouseup="parar(event)">▼</button><button class="vazio"></button>
</div>
<div class="vel">Velocidade: <b id="vv">__VEL__</b>
  <input type="range" min="0" max="255" value="__VEL__" id="sl" oninput="document.getElementById('vv').innerText=this.value" onchange="cmd('v='+this.value)">
</div>
<div class="st" id="st">-</div>
<script>
function cmd(q){fetch('/cmd?'+q).then(r=>r.text()).then(t=>document.getElementById('st').innerText=t).catch(e=>{})}
function ir(d,e){if(e)e.preventDefault();cmd('d='+d)}
function parar(e){if(e)e.preventDefault();cmd('d=s')}
setInterval(function(){fetch('/status').then(r=>r.json()).then(j=>document.getElementById('st').innerText='ação: '+j.acao+' · vel: '+j.velocidade+' · IP: '+j.ip).catch(e=>{})},1500)
</script></body></html>)HTML";

// ---------------------------------------------------------------- rotas
void rotaPagina() { server.send_P(200, "text/html", PAGINA); }

void rotaCmd() {
  if (server.hasArg("v")) {
    int v = server.arg("v").toInt();
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    velocidade = v;
    if (acao != 's') aplicar(acao);   // aplica na hora se estiver andando
  }
  if (server.hasArg("d")) {
    char d = server.arg("d").charAt(0);
    aplicar(d);
  }
  String r = "acao=" + String(acao) + " velocidade=" + String(velocidade) + " ip=" + WiFi.localIP().toString();
  server.send(200, "text/plain", r);
}

void rotaStatus() {
  String j = "{\"acao\":\"" + String(acao) + "\",\"velocidade\":" + String(velocidade)
           + ",\"ip\":\"" + WiFi.localIP().toString() + "\",\"comandos\":" + String(contador) + "}";
  server.send(200, "application/json", j);
}

// ---------------------------------------------------------------- setup
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("=== Carrinho 4WD (NodeMCU + L293D) ==="));

  pinMode(PIN_DIR_VEL, OUTPUT); pinMode(PIN_DIR_DIR, OUTPUT);
  pinMode(PIN_ESQ_VEL, OUTPUT); pinMode(PIN_ESQ_DIR, OUTPUT);
  analogWriteRange(255);
  analogWriteFreq(1000);
  aplicar('s');

  WiFi.hostname(HOSTNAME);      // nome no DHCP (aparece como "carro4wd" no Pi-hole)
  WiFi.mode(WIFI_STA);
  Serial.print(F("MAC do carrinho: "));
  Serial.println(WiFi.macAddress());
  Serial.print(F("hostname: "));
  Serial.println(WiFi.hostname());
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print(F("conectando ao WiFi "));
  Serial.print(WIFI_SSID);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(300);
    Serial.print('.');
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print(F("IP: ")); Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("\nWiFi de casa nao respondeu -> criando o AP do carrinho"));
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print(F("AP ")); Serial.print(AP_SSID);
    Serial.print(F("  IP: ")); Serial.println(WiFi.softAPIP());
  }

  server.on("/", rotaPagina);
  server.on("/cmd", rotaCmd);
  server.on("/status", rotaStatus);
  server.onNotFound(rotaPagina);
  server.begin();
  Serial.println(F("pagina no ar. comandos: f/t/e/d/s + v=0..255"));
}

// ---------------------------------------------------------------- loop
void loop() {
  server.handleClient();

  // dead man: se estava andando e o celular parou de mandar, PARA
  if (acao != 's' && millis() - ultimoComando > DEADMAN_MS) {
    aplicar('s');
    ultimoComando = millis();
    Serial.println(F("[deadman] parado por falta de comando"));
  }
}
