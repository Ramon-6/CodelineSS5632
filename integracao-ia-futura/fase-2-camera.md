# Fase 2 — Câmera com IA (UnitV2 ou Module LLM)

Só valem a pena se a Fase 1 (sensor) não resolver, ou seja: se a esteira
tiver interferências que fazem o sensor disparar errado (poeira, outras
peças passando que não deveriam contar, etc).

## Hardware recomendado

**M5Stack UnitV2** — melhor custo-benefício para detecção de presença +
classificação simples. Alternativa: **Module LLM** se precisar OCR ou
descrição em linguagem natural.

Cabo Grove para Grove de 4 pinos (vem com o módulo).

## Montagem

```
StamPLC                      UnitV2
┌──────────┐                 ┌──────────┐
│   Grove  │────Grove────────│   Grove  │
│   5V ────│─────────────────│─── 5V    │
│   GND ───│─────────────────│─── GND   │
│   TX ────│─────────────────│─── RX    │
│   RX ────│─────────────────│─── TX    │
└──────────┘                 └──────────┘
```

- 5V do PLC alimenta o UnitV2 (verificar consumo — pode precisar fonte
  separada se o UnitV2 for puxar >400 mA)
- TX do PLC em RX da câmera e vice-versa
- Baud 115200

## Treinamento do modelo

### Caminho 1 — modelo pronto do UnitV2

Interface web (http://10.254.239.1 depois de conectar o UnitV2 por USB):
- "Classifier" → treinar 2 classes: "peca", "vazio"
- Capturar 50-100 fotos de cada classe diretamente pela interface
- Treinar (minutos) e aplicar

### Caminho 2 — YOLO custom

- Capturar 500+ imagens anotadas (recomendo CVAT ou LabelImg)
- Treinar YOLO v5 nano no PC
- Converter para formato aceito pelo UnitV2 (`.axera` ou `.k210` dependendo
  da variante)
- Transferir por SCP (SSH no UnitV2 habilitado via config)

Caminho 1 é 10x mais rápido e geralmente suficiente para esse caso.

## Protocolo UART no UnitV2

O UnitV2 já tem um script padrão `function.json` que roda ao boot. Para
nossa integração, criar `/userdata/function/pipeline.py`:

```python
import json
import sys
import time
from axengine import load_model

modelo = load_model('/userdata/function/modelo_peca.axera')

while True:
    frame = capture()
    out = modelo.predict(frame)
    peca_detectada = out['class'] == 'peca'
    conf = float(out['conf'])
    print(json.dumps({
        'peca': peca_detectada,
        'conf': round(conf, 2),
        'n': 1 if peca_detectada else 0,
    }), flush=True)
    time.sleep(0.3)
```

O `print` sai no UART do Grove automaticamente (stdout do UnitV2 é
redirecionado para o serial).

## Lado do PLC

Adicionar leitura UART no loop principal:

```cpp
#include <HardwareSerial.h>
HardwareSerial Cam(2);  // UART2 do ESP32-S3

void setup() {
    // ... código existente ...
    Cam.begin(115200, SERIAL_8N1, GROVE_RX_PIN, GROVE_TX_PIN);
}

char linhaCam[128];
size_t linhaCamPos = 0;
bool temPecaCam = false;
uint32_t ultimaPecaCam = 0;
uint32_t ultimaMsgCam = 0;

void lerCamera() {
    while (Cam.available()) {
        char c = Cam.read();
        if (c == '\n' || linhaCamPos >= sizeof(linhaCam) - 1) {
            linhaCam[linhaCamPos] = 0;
            processarLinhaCam(linhaCam);
            linhaCamPos = 0;
        } else if (c >= 32) {
            linhaCam[linhaCamPos++] = c;
        }
    }
}

void processarLinhaCam(const char* linha) {
    // JSON parse ligeiro (sem lib, procurando os campos)
    const char* p = strstr(linha, "\"peca\":");
    if (!p) return;
    temPecaCam = (strstr(p, "true") && (strstr(p, "true") - p < 10));
    const char* c = strstr(linha, "\"conf\":");
    float conf = 0;
    if (c) conf = atof(c + 7);
    ultimaMsgCam = millis();
    if (temPecaCam && conf >= 0.70f) {
        ultimaPecaCam = millis();
    }
}

bool cameraOnline() {
    return (millis() - ultimaMsgCam) < 10000;  // viva se mandou msg há <10s
}
```

E na máquina de estados, no RODANDO:

```cpp
case RODANDO: {
    lerCamera();
    bool fimDePeca;
    if (cameraOnline()) {
        fimDePeca = (millis() - ultimaPecaCam) > CAMERA_SEM_PECA_S * 1000UL;
    } else {
        // fallback: modo tempo
        fimDePeca = (millis() - inicioRodando) > RELE_TEMPO_MS;
    }
    if (fimDePeca) {
        irPara(PULSA_PARA);
    }
    break;
}
```

## Validação visual

Opcional, mas útil: botão/opção na interface web do PLC "ver câmera" que
abre um stream do UnitV2 (http://10.254.239.1/stream, se habilitado) em
nova aba.

## Métricas que valem a pena coletar

Já que a câmera dá contagem real, logar no CSV diário:
- peças detectadas / minuto
- duração real de cada lote
- intervalo entre peças (identifica gargalo na linha anterior)
- confiança média do modelo (se cair, modelo está perdendo qualidade,
  hora de retreinar)

## Troca para Module LLM

Se depois quiser mudar para Module LLM (para OCR ou VLM), **o protocolo
UART é idêntico**. Troca-se o hardware e o script embarcado, o firmware
do PLC não precisa mudar.
