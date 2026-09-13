/* Diagnóstico: por que o WiFi não conecta? Mostra o status e o REASON do SDK. */
#include <ESP8266WiFi.h>
#include "config.h"

extern "C" {
#include "user_interface.h"
}

const char* nomeStatus(int s) {
  switch (s) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "SEM SSID (nao achou a rede)";
    case WL_SCAN_COMPLETED: return "scan ok";
    case WL_CONNECTED: return "CONECTADO";
    case WL_CONNECT_FAILED: return "FALHA AO CONECTAR";
    case WL_WRONG_PASSWORD: return "SENHA ERRADA";
    case WL_DISCONNECTED: return "DESCONECTADO";
    default: return "?";
  }
}

const char* nomeReason(int r) {
  switch (r) {
    case 1: return "UNSPECIFIED";
    case 2: return "AUTH_EXPIRE";
    case 3: return "AUTH_LEAVE";
    case 4: return "ASSOC_EXPIRE";
    case 5: return "ASSOC_TOOMANY";
    case 6: return "NOT_AUTHED";
    case 7: return "NOT_ASSOCED";
    case 8: return "ASSOC_LEAVE";
    case 9: return "ASSOC_NOT_AUTHED";
    case 15: return "4WAY_HANDSHAKE_TIMEOUT (senha/PMF)";
    case 16: return "GROUP_KEY_UPDATE_TIMEOUT";
    case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    default: return "outro";
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("=== DIAG WiFi ==="));
  Serial.print(F("SSID alvo: ")); Serial.println(WIFI_SSID);
  WiFi.persistent(false);
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);   // teste: forca 802.11g (alguns roteadores implicam com b/g/n)
  WiFi.setOutputPower(17);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  int ultimoStatus = -1;
  int ultimoReason = -1;
  while (millis() - t0 < 40000) {
    delay(1000);
    int st = WiFi.status();
    int rs = wifi_station_get_connect_status();
    if (st != ultimoStatus || rs != ultimoReason) {
      Serial.print(F("t="));
      Serial.print((millis() - t0) / 1000);
      Serial.print(F("s  status="));
      Serial.print(st);
      Serial.print(F(" ("));
      Serial.print(nomeStatus(st));
      Serial.print(F(")  reason="));
      Serial.print(rs);
      Serial.print(F(" ("));
      Serial.print(nomeReason(rs));
      Serial.println(F(")"));
      ultimoStatus = st;
      ultimoReason = rs;
    }
    if (st == WL_CONNECTED) {
      Serial.print(F(">>> CONECTOU! IP: "));
      Serial.println(WiFi.localIP());
      Serial.print(F(">>> gateway: "));
      Serial.println(WiFi.gatewayIP());
      Serial.print(F(">>> canal: "));
      Serial.println(WiFi.channel());
      Serial.print(F(">>> BSSID: "));
      Serial.println(WiFi.BSSIDstr());
      Serial.print(F(">>> RSSI: "));
      Serial.println(WiFi.RSSI());
      return;
    }
  }
  Serial.println(F("=== fim (nao conectou) ==="));
}

void loop() { delay(5000); }
