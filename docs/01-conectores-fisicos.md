# Conectores físicos

Baseado nos PDFs `novo sistema io.pdf` e `IO interface -20191108.pdf` do
fabricante.

A impressora SS5632 tem quatro conectores circulares na parte traseira,
cada um com uma função. Todos são M12 ou M8 industrial.

## Encoder (7 pinos)

Para sincronizar a impressão com o movimento da fita. A impressora só
imprime enquanto este encoder gira — é por isso que a esteira precisa
estar rodando.

| Pino | Função |
|---|---|
| 1 | 12 V+ (Power+) |
| 2 | 0 V (Power−) |
| 3 | EN — Encoder Enable |
| 4 | A — Encoder A (pulso) |
| 5 | Printing (sinal de impressão emitido pela impressora) |
| 6 | 24 V+ |
| 7 | GND (24 V−) |

**Pino 5 (Printing signal) é uma SAÍDA da impressora** — pulsa a cada
impressão real. Pode ser usado para contar produtos impressos sem passar
pelo Modbus. Não usamos no projeto, mas é opção futura.

## Sensor (5 pinos) — fotocélula

Detecta a passagem do produto na esteira. Dispara a impressão.

| Pino | Função |
|---|---|
| 1 | 12 V+ |
| 2 | 0 V |
| 4 | Signal (saída da fotocélula) |
| 5 | GND (shield) |

Impressora espera o pulso do sensor para imprimir sincronizado com o
produto.

## I/O (7 pinos) — BCD + reset + direção

| Pino | Função |
|---|---|
| 1 | 12 V+ |
| 2 | BCD bit 0 |
| 3 | PRINTDIR — Direção de impressão (alto = regular, baixo = inversa) |
| 4 | 0 V |
| 5 | 0 V (GND) |
| 6 | BCD bit 1 (fonte: /Counter 1 reset) |
| 7 | BCD bit 3 (fonte: /Counter 1 reset) |

Documentação de 2019 (`IO interface -20191108.pdf`) mostra também
**BCD bit 2 / Counter 2 reset** nos pinos. Dependendo do firmware a
pinagem pode variar — meça com multímetro antes de ligar.

**BCD é usado para alternar entre mensagens pré-gravadas** sem passar
pela rede. Não usamos.

## Alarme (7 pinos) — luzes + RS232

| Pino | Função |
|---|---|
| 1 | Luz verde (printer ready) |
| 2 | Luz amarela (warning signal) |
| 3 | RXD (RS232 recepção da impressora) |
| 4 | 24 V+ |
| 5 | 0 V (GND) |
| 6 | TXD (RS232 transmissão da impressora) |
| 7 | Luz vermelha (printer fault) |

**Luzes são saídas da impressora** — podem ser lidas por uma entrada
digital do PLC para monitorar status sem Modbus.

A RS232 do pino 3/6 pode ser usada para o mesmo protocolo privado da
porta 4008 (precisa configurar na tela "Serial Port Setting" dentro da
impressora). **Importante**: habilitar essa serial desabilita a
Ethernet — veja [lições aprendidas](05-licoes-aprendidas.md).

## Esquema simplificado para nossa integração

```
IMPRESSORA                          STAMPLC
───────────                         ────────
Encoder pino 1 (+12V) ──┐
Encoder pino 2 (0V)   ──┼── Esteira
                        │
Fotocélula            ──┼── Esteira (produto)
                        │
Botão esteira (NA) ─────┼── R1 COM/NA do StamPLC (em paralelo)
                        │
LED botão (+24V) ───────┼── IN1 do StamPLC
LED botão (0V) ─────────┴── COM das entradas do StamPLC

RJ45 ─────────────────────── Switch ─── StamPLC PoE (RJ45)
                                     └── Notebook (opcional)
```
