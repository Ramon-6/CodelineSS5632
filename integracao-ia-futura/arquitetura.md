# Arquitetura

Como a câmera com IA e o StamPLC se comunicam, e como isso se encaixa
no ciclo da esteira que já existe.

## Visão geral

```
┌──────────────┐       ┌────────────────┐       ┌──────────────┐
│  Impressora  │──LAN──│    StamPLC     │──Grove│  Câmera IA   │
│   SS5632     │ 1502  │   M5Stack      │ UART  │  (UnitV2 ou  │
│              │ 4008  │                │       │   Module LLM)│
└──────┬───────┘       └────────┬───────┘       └──────┬───────┘
       │ scanner                │ R1/R2 relé           │ olha pra
       │ USB                    │ 24V botão            │ esteira
       │                        ▼                      │
       ▼                 ┌─────────────┐               │
┌─────────────┐          │   Botão     │               │
│  Operador   │          │   esteira   │               │
└─────────────┘          └──────┬──────┘               │
                                │ 24V LED              │
                                ▼                      │
                         ┌─────────────┐               │
                         │   Esteira   │◀──────────────┘
                         │             │  câmera olha a entrada
                         └─────────────┘  (ou a saída)
```

## Papéis

### Impressora Codeline (já existe)
- Lê o scanner USB → grava código em `4x0201`
- Imprime o código em cada peça que passa pela fotocélula

### StamPLC (já existe, vai receber upgrade)
- Monitora `4x0201` → detecta nova leitura
- Aciona relé R1 → liga esteira
- **NOVO**: lê UART Grove → recebe status "tem peça / não tem peça"
- **NOVO**: lógica de parada = sem peça por N segundos (não mais tempo fixo)
- Aciona relé R1 → desliga esteira
- Comando `0x33` na impressora → apaga código

### Câmera IA (novo)
- Observa um ponto fixo da esteira (entrada ou saída, dependendo do caso)
- Roda modelo de detecção localmente
- Envia status via UART para o PLC a cada N ms

## Protocolo UART

Mantemos simples: **JSON linha a linha a 115200 bps 8N1**.

### Mensagem da câmera para o PLC

A câmera envia a cada 200-500 ms uma linha de texto JSON terminada por `\n`:

```json
{"peca": true, "conf": 0.92, "n": 1}
{"peca": false, "conf": 0.00, "n": 0}
{"peca": true, "conf": 0.87, "n": 2}
```

Campos:
- `peca` — boolean, se tem pelo menos uma peça visível
- `conf` — confiança da detecção (0.0 a 1.0)
- `n` — quantas peças a câmera está vendo agora (0 se nenhuma)

PLC só considera `peca=true` quando `conf > 0.70` (configurável), para
reduzir falsos positivos.

### Mensagem do PLC para a câmera (opcional)

```json
{"cmd": "ping"}
{"cmd": "set_target", "target": "borracha"}
{"cmd": "snapshot"}
```

Primeira fase ignora comandos do PLC para a câmera. A câmera só manda,
nunca recebe. Simplifica muito.

## Máquina de estados do PLC — nova versão

Comparado ao firmware atual, muda a saída do estado `RODANDO`:

```
┌──────────┐
│  OCIOSO  │◀───── reload template → apaga código
└────┬─────┘
     │ 4x0201 muda (nova leitura)
     ▼
┌──────────┐
│  LIGAR   │ pulsa R1 por 1 s
└────┬─────┘
     │ IN1 (LED do botão) ligou
     ▼
┌──────────┐
│ RODANDO  │ se CAMERA_HABILITADA:
│          │   enquanto câmera vê peça → fica aqui
│          │   SEM peça por N seg → vai pra PARAR
│          │ senão (modo tempo):
│          │   roda tempo_configurado → vai pra PARAR
└────┬─────┘
     │
     ▼
┌──────────┐
│  PARAR   │ pulsa R1 por 1 s
└────┬─────┘
     │ IN1 desligou
     ▼
┌──────────┐
│ LIMPANDO │ comando 0x33 na impressora
└────┬─────┘
     │
     └──→ volta para OCIOSO
```

## Tolerância a câmera com defeito

Se a câmera parar de responder (sem linha UART por 10 s), o PLC
**volta ao modo tempo** automaticamente, usando o tempo configurado
como se nada tivesse mudado. Isso evita que a produção pare quando a
câmera travar — e dá tempo pro técnico resolver sem urgência.

Logar o evento no SD e piscar aviso no display: "camera offline, modo
tempo ativo".

## Posição da câmera

**Entrada da esteira**: detecta se tem peça entrando. Serve para parar
a esteira quando o fluxo de peças acaba.

**Saída da esteira**: detecta se tem peça saindo. Serve para contar
produção real.

**Lateral**: útil só para leitura do código impresso (OCR) com
Module LLM.

Primeira fase: **entrada**. Mais simples, resposta imediata.

## Alimentação

A câmera precisa de 5 V. O conector Grove do PLC fornece 5 V @ ~500 mA,
suficiente para o UnitV2 (~250 mA pico). O Module LLM consome mais e
pode precisar de fonte externa.

## Latência total

- Câmera → inferência: 30-100 ms (UnitV2) ou 200-800 ms (Module LLM)
- UART @ 115200: 1-2 ms por linha
- Loop do PLC: 50 ms

Soma: **100-900 ms**. Para o caso da Sumiriko (esteira lenta de borracha),
está muito confortável.
