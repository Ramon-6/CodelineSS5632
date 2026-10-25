# Código de exemplo — esqueleto

Esse código **não está integrado ao firmware atual**. É só um esboço
pronto para colar quando for implementar.

## config.h — novos defines

```cpp
// === Visão (sensor ou câmera) ===
#define VISAO_HABILITADA        0       // 0=modo tempo, 1=sensor, 2=cam UART
#define VISAO_PINO_SENSOR       1       // IN2 (quando VISAO_HABILITADA=1)
#define VISAO_SEM_PECA_S        15      // sem peça por N seg → para
#define VISAO_CONF_MINIMA       70      // confianca minima (0-100) p/ camera
#define VISAO_TIMEOUT_MS        10000   // sem msg da camera por N ms → fallback

// === Pinos Grove do StamPLC (ESP32-S3) ===
#define GROVE_TX_PIN            32      // conferir no esquemático do StamPLC
#define GROVE_RX_PIN            33

// === Tempo máximo absoluto (fallback) ===
#define TEMPO_MAX_ABS_MS        (RELE_TEMPO_MS * 2)
```

## visao.h — módulo novo

```cpp
#pragma once
#include <Arduino.h>
#include "config.h"

namespace Visao {

enum Modo { DESABILITADA, SENSOR, CAMERA };

void inicializar();
void processar();              // chamar no loop principal
bool temPeca();                // instantâneo
bool fimDePeca(uint32_t desde);// true se não vê peça há VISAO_SEM_PECA_S s
bool online();                 // só relevante no modo CAMERA
int  ultimaConfianca();        // 0-100, só modo CAMERA
int  pecasAgora();             // só modo CAMERA, 0 se não suporta

} // namespace Visao
```

## visao.cpp — módulo novo

```cpp
#include "visao.h"
#include <HardwareSerial.h>
#include <M5StamPLC.h>

namespace Visao {

static HardwareSerial Cam(2);
static Modo modo = DESABILITADA;
static uint32_t ultPecaVista = 0;
static uint32_t ultMsgCam = 0;
static int confAtual = 0;
static int nAtual = 0;
static bool pecaAgora = false;

static char buf[128];
static size_t bufPos = 0;

static void processarLinha(const char* linha) {
    const char* p = strstr(linha, "\"peca\":");
    if (!p) return;
    bool detectou = false;
    const char* tr = strstr(p, "true");
    if (tr && tr - p < 12) detectou = true;
    int conf = 0;
    const char* c = strstr(linha, "\"conf\":");
    if (c) {
        float f = atof(c + 7);
        conf = (int)(f * 100);
    }
    int n = 0;
    const char* nn = strstr(linha, "\"n\":");
    if (nn) n = atoi(nn + 4);

    ultMsgCam = millis();
    confAtual = conf;
    nAtual = n;
    pecaAgora = (detectou && conf >= VISAO_CONF_MINIMA);
    if (pecaAgora) ultPecaVista = millis();
}

void inicializar() {
    #if VISAO_HABILITADA == 1
        modo = SENSOR;
    #elif VISAO_HABILITADA == 2
        modo = CAMERA;
        Cam.begin(115200, SERIAL_8N1, GROVE_RX_PIN, GROVE_TX_PIN);
    #else
        modo = DESABILITADA;
    #endif
}

void processar() {
    if (modo == SENSOR) {
        pecaAgora = M5StamPLC.readPlcInput(VISAO_PINO_SENSOR);
        if (pecaAgora) ultPecaVista = millis();
        return;
    }
    if (modo == CAMERA) {
        while (Cam.available()) {
            char c = Cam.read();
            if (c == '\n' || bufPos >= sizeof(buf) - 1) {
                buf[bufPos] = 0;
                if (bufPos > 2) processarLinha(buf);
                bufPos = 0;
            } else if (c >= 32) {
                buf[bufPos++] = c;
            }
        }
    }
}

bool temPeca()      { return pecaAgora; }
bool online()       { return modo != CAMERA || (millis() - ultMsgCam) < VISAO_TIMEOUT_MS; }
int  ultimaConfianca() { return confAtual; }
int  pecasAgora()   { return nAtual; }

bool fimDePeca(uint32_t desdeMs) {
    (void)desdeMs;
    if (modo == DESABILITADA) return false;
    if (modo == CAMERA && !online()) return false;  // fallback
    return (millis() - ultPecaVista) > (uint32_t)VISAO_SEM_PECA_S * 1000UL;
}

} // namespace Visao
```

## main.cpp — mudanças na máquina de estados

```cpp
// no setup():
Visao::inicializar();

// no loop():
Visao::processar();

// no estado RODANDO:
case RODANDO: {
    bool fim = false;
    if (Visao::modo != Visao::DESABILITADA && Visao::online()) {
        fim = Visao::fimDePeca(0);
    } else {
        fim = (millis() - inicioRodando) >= RELE_TEMPO_MS;
    }
    // trava absoluta
    if ((millis() - inicioRodando) >= TEMPO_MAX_ABS_MS) {
        fim = true;
        logEvento("TRAVA_TEMPO_MAX");
    }
    if (fim) irPara(PULSA_PARA);
    break;
}
```

## Script do UnitV2 (pipeline.py)

```python
#!/usr/bin/env python3
"""UnitV2 pipeline — detecta peca e envia JSON pelo UART."""
import json
import time
import sys
from axengine import load_model, open_camera

cam = open_camera(resolution=(640, 480))
modelo = load_model('/userdata/function/modelo_peca.axera')

CLASSES = ['vazio', 'peca']

while True:
    frame = cam.read()
    if frame is None:
        continue
    out = modelo.predict(frame)
    idx = int(out['class'])
    conf = float(out['conf'])
    tem_peca = (CLASSES[idx] == 'peca')
    sys.stdout.write(json.dumps({
        'peca': tem_peca,
        'conf': round(conf, 2),
        'n': 1 if tem_peca else 0,
    }) + '\n')
    sys.stdout.flush()
    time.sleep(0.3)
```

## Testes bancada antes de ir para a fábrica

1. Esqueleto do simulador: ligar UnitV2 no PC via USB-C, abrir
   serial monitor, confirmar que o JSON sai.
2. Trocar USB-C por cabo Grove no StamPLC, confirmar PLC lê.
3. No modo bancada, forçar `Visao::modo = CAMERA` via web interface.
4. Levar peça e peça falsa (lixo) para calibrar confiança mínima.
5. Validar fallback: desligar UnitV2 em pleno RODANDO, confirmar que
   PLC continua operando pelo tempo configurado.
