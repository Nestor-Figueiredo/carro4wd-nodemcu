/* Diagnóstico: lista as redes WiFi que o ESP8266 (2,4 GHz) enxerga.
 * Flash temporário só para descobrir SSID/canal/segurança. */
#include <ESP8266WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(300);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(200);
  Serial.println();
  Serial.println(F("=== SCAN WiFi (2,4 GHz) ==="));
  int n = WiFi.scanNetworks();
  Serial.print(F("redes encontradas: "));
  Serial.println(n);
  for (int i = 0; i < n; i++) {
    String seg;
    switch (WiFi.encryptionType(i)) {
      case ENC_TYPE_NONE: seg = "ABERTA"; break;
      case ENC_TYPE_WEP: seg = "WEP"; break;
      case ENC_TYPE_TKIP: seg = "WPA-TKIP"; break;
      case ENC_TYPE_CCMP: seg = "WPA2"; break;
      case ENC_TYPE_AUTO: seg = "WPA/WPA2"; break;
      default: seg = "tipo " + String(WiFi.encryptionType(i)); break;
    }
    Serial.print(i + 1);
    Serial.print(F(") "));
    Serial.print(WiFi.SSID(i));
    Serial.print(F("  ch="));
    Serial.print(WiFi.channel(i));
    Serial.print(F("  rssi="));
    Serial.print(WiFi.RSSI(i));
    Serial.print(F("  "));
    Serial.println(seg);
  }
  Serial.println(F("=== fim do scan ==="));
}

void loop() { delay(10000); }
