# Hardware — opções e comparação

## O conector Grove do StamPLC

O StamPLC traz uma porta **Grove de 4 pinos** (vermelho) ligada ao
ESP32-S3 interno. Esse conector carrega **três protocolos possíveis**
nos mesmos fios físicos, dependendo de como o firmware configura:

| Pino | Cor | Função I2C | Função UART | Função GPIO |
|---|---|---|---|---|
| 1 | Vermelho | 5 V | 5 V | 5 V |
| 2 | Preto | GND | GND | GND |
| 3 | Branco | SDA | TX (do PLC) | GPIO |
| 4 | Amarelo | SCL | RX (do PLC) | GPIO |

- **I2C**: para sensores pequenos (temperatura, acelerômetro, OLED).
  Não dá para câmera.
- **UART**: serial bidirecional. Serve para falar com módulos que já
  processam a imagem internamente e devolvem só o resultado.
- **GPIO digital**: dois fios livres, dá para ler sensor fotoelétrico
  simples (24 V convertido) ou mandar sinal para outro módulo.

## Opção 1 — Sensor fotoelétrico (recomendado começar)

**Hardware**: sensor fotoelétrico retrorreflexivo industrial, 24 V,
saída PNP, ~R$ 50-150.

**Como funciona**: emite um feixe de luz; quando uma peça interrompe o
feixe, muda a saída. É o mesmo princípio da fotocélula que já está na
impressora, só que agora na posição do PLC.

**Ligação ao StamPLC**: já temos IN1 do PLC usado para LED do botão.
Sobram 7 entradas isoladas (IN2 a IN8), tolerantes 5-36 V. Ligar
diretamente — sem conversor.

**Vantagens**:
- Barato (R$ 50-150)
- Sem programação de IA (basta ler a entrada digital)
- Resposta em milissegundos (não precisa de ciclo de inferência)
- Imune a mudança de iluminação, poeira moderada, etc

**Limitação**:
- Detecta QUALQUER coisa passando — não distingue peça de resíduo

Para o uso da Sumiriko (fitas de borracha padronizadas numa esteira
dedicada), isso **provavelmente é suficiente**. Faria sentido pular IA
inteiramente.

## Opção 2 — M5Stack UnitV2

**Hardware**: unidade da M5Stack com Sigmstar SSD202D (dual Cortex-A7 1,2 GHz),
GC2145 câmera 1600×1200, 128 MB RAM, 512 MB flash, rodando Linux buildroot.
~R$ 650.

**IA onde**: no próprio módulo. Tem framework interno de modelos (classificação,
detecção, OCR, código de barras, reconhecimento de cor/forma/rostos).

**Como fala com PLC**:
- Via **UART no Grove** (pinos TX/RX conectam no Grove do PLC)
- Ou via **USB-C** para servidor com `pyserial`
- Protocolo é JSON linha a linha pelo UART

**Como treinar modelo próprio**:
- Upload de dataset pela interface web do UnitV2
- Ou SSH + rodar scripts Python internos (buildroot Linux)
- Pode usar YOLOv5 nano ou MobileNet SSD

**Prós**:
- Autônomo (nada no PC)
- Interface web para configurar
- API UART simples: `{"running":"obj_detect","target":[...]}` chegando
  linha a linha
- Permite trocar o modelo sem mexer no PLC

**Contras**:
- A IA interna é limitada aos modelos que a M5 disponibiliza
- Treinar modelo próprio exige tempo e dataset grande
- A documentação da M5 nessa família é inconsistente

## Opção 3 — M5Stack Module LLM

**Hardware**: módulo baseado no AX630C (chip de inferência da Axera),
3,2 TOPS NPU, 4 GB LPDDR4, 32 GB eMMC, câmera opcional. ~R$ 900.

**IA onde**: no próprio módulo. Roda **LLMs multimodais quantizados**
(Qwen-VL 2B, YOLO, etc) completamente offline.

**Como fala com PLC**: UART Grove, ou SPI/USB-C.

**Prós**:
- Pode fazer tarefas de visão sofisticadas (ler números em peças,
  descrever cenas, perguntar "tem peça da família X na esteira?")
- VLM offline — sem nuvem
- Datasheet documentado da M5

**Contras**:
- Caro (quase 2x UnitV2)
- Latência de VLM é alta (1-3 s por frame)
- Overkill para só detectar presença de peça

Boa escolha se o objetivo evoluir para OCR do código impresso (fechando
o loop: impressora imprime → câmera lê o impresso → confirma leitura
correta).

## Opção 4 — ESP32-CAM + servidor local

**Hardware**: ESP32-CAM da AI-Thinker (~R$ 60) + um PC/mini-PC na rede
local rodando servidor Python com YOLO/MediaPipe.

**IA onde**: no servidor (PC). A câmera só manda frames.

**Como fala com PLC**: via WiFi/HTTP ou MQTT.
- ESP32-CAM manda frames JPEG para o servidor
- Servidor roda inferência e expõe `/status` com resultado JSON
- PLC faz GET no servidor a cada 500 ms

**Prós**:
- Barato de hardware
- Modelo rodando no PC pode ser qualquer coisa (YOLO v8, SAM, etc)
- Fácil de debugar (log no PC)

**Contras**:
- Depende de PC ligado e disponível 24/7
- Latência de rede
- Ponto único de falha extra
- Pode conflitar com os sockets limitados do W5500 do PLC

Boa escolha se já tem um servidor industrial na fábrica.

## Opção 5 — Câmera IP + VLM na nuvem

Não recomendamos para esse projeto. Fábrica raramente tem internet
confiável, e mandar imagens de produção para nuvem levanta questões
de confidencialidade industrial.

## Recomendação

1. **Primeiro**: instalar sensor fotoelétrico (opção 1). Baixo custo,
   zero software de IA, resolve o problema de "quando parar a esteira".
2. **Se precisar discriminar peças**: UnitV2 (opção 2) — melhor custo-benefício.
3. **Se precisar ler o código impresso**: Module LLM (opção 3) — permite
   fechar o loop visualmente.
