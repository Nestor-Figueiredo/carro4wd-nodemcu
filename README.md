# 🚗 Carro 4WD — NodeMCU (ESP-12E) + Motor Shield L293D

Firmware do carrinho 4WD controlado por **WiFi**, com página de controle no celular.
Projeto **156 "Auto motor 4WD"** (a ideia original é levar uma **ESP32-CAM** no chassi).

```
[celular] ──WiFi──▶ [NodeMCU + shield L293D] ──▶ [4 motores (2 canais)]
```

## Hardware

| Item | Detalhe |
|---|---|
| Placa | NodeMCU **ESP-12E** (**V2** — a shield não encaixa no V3) |
| Shield | "ESP Motor Shield" **HW-588A** (CI **L293D**, 2 pontes H) |
| Chassi | 4WD — os 2 motores da **direita** juntos e os 2 da **esquerda** juntos (2 canais, estilo "tanque") |
| Bateria | 4 pilhas AA no conector do shield (GND comum com o NodeMCU) |

### Pinos (confirmados no tutorial do blog Eletrogate)

| Motor | Velocidade (PWM) | Direção |
|---|---|---|
| **Direito** | **D1** (GPIO5) | **D3** (GPIO0) |
| **Esquerdo** | **D2** (GPIO4) | **D4** (GPIO2) |

> Para alimentar o ESP pela mesma bateria (máx. 7 V), coloque o **jumper entre Vin e Vm**.
> Se o ESP desconectar, alimente separado pelo conector *ESP Power*.

## Como usar

1. Grava o firmware e liga o carrinho — ele entra no **WiFi de casa** e imprime o **IP** na serial (115200).
2. Abre esse IP no celular: **▲ ▼ ◀ ▶** (segurar = anda, soltar = para), **slider de velocidade** (0–255) e **■** (parar).
3. **Dead man**: sem comando por **700 ms**, o carro **para sozinho**.
4. Sem WiFi de casa, ele cria o AP **`4WD-Car`** com a mesma página.

## API (integração futura com o dashboard/openHAB)

```
GET /cmd?d=f|t|e|d|s&v=0..255     # frente/trás/esquerda/direita/parar + velocidade
GET /status                        # {"acao":"f","velocidade":200,"ip":"...","comandos":12}
```

## Compilar e gravar (no pip5)

```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 carro4wd
arduino-cli upload  -p /dev/ttyUSB0 --fqbn esp8266:esp8266:nodemcuv2 carro4wd
```

Requer o core **`esp8266:esp8266`** (`arduino-cli core install esp8266:esp8266`, com o índice
`http://arduino.esp8266.com/stable/package_esp8266com_index.json`).

## Configuração local

As credenciais de WiFi e os pinos ficam em `carro4wd/config.h` (**não versionado**):

```bash
cp carro4wd/config.h.example carro4wd/config.h   # edite SSID/senha
```

Ajustes úteis no `config.h`:
- `MOTOR_DIR_INVERTIDO` / `MOTOR_ESQ_INVERTIDO` → se um lado girar ao contrário;
- `VEL_PADRAO` → velocidade inicial;
- `DEADMAN_MS` → tempo sem comando para parar;
- `AP_SSID` / `AP_PASS` → rede de emergência.

## Próximos passos

- [ ] Testar com o carrinho **levantado** (rodas no ar) e depois no chão
- [ ] Câmera **ESP32-CAM** no chassi (ideia original do projeto 156)
- [ ] Integrar a API no Dashboard Onix (botões/setas na tela)
