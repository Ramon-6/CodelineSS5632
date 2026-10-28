# Impressora Codeline SS5632 — Manual Técnico

Documentação consolidada da impressora inkjet industrial Codeline SS5632
que a Sumiriko usa para marcar lotes em fitas de borracha. Juntei aqui
os três manuais originais do fabricante com tudo que aprendemos na
prática durante a integração — porque **vários pontos do manual são
enganosos ou incompletos**, e documentar isso evita horas perdidas da
próxima vez.

## Modelo e versões

| Item | Valor |
|---|---|
| Modelo | Codeline SS5632 |
| Kernel | U-5.0.8-H3 |
| Aplicação | FKK-5.1.13.0 |
| Driver | LI-5.3.3N-H3 |
| Protocolo (porta 4008) | D-1.0.3.0b |

## Índice

1. [Conectores físicos](01-conectores-fisicos.md) — Encoder, Sensor
   (fotocélula), I/O (BCD + counter reset + print direction), Alarme
   (luzes + RS232)
2. [Portas de rede](02-portas-rede.md) — 1502, 4008, 4040, 4002, 8017,
   21, 23
3. [Modbus TCP (porta 1502)](03-modbus-tcp.md) — mapa completo de
   registros, coils, discrete inputs
4. [Protocolo privado (porta 4008)](04-porta-4008.md) — comandos de
   controle, impressão, template, callback
5. [Lições aprendidas](05-licoes-aprendidas.md) — o que o manual não
   diz, o que é falso e como contornar
6. [Ligação física](06-ligacao-fisica.md) — diagrama elétrico do painel
   (fonte, relés, entradas, sirene, botão modo manual)
7. [Manutenção em campo](07-manutencao-em-campo.md) — AP WiFi + OTA +
   Grove UART para usar o Cardputer no lugar do notebook

## Convenção de endereços

O manual Modbus do fabricante usa a notação **4x0001-base** (padrão
Modicon antigo). O endereço real que vai no fio (Modbus wire) é
**base-0**:

| Notação do manual | Função | Endereço wire | Observação |
|---|---|---|---|
| 3x0001 | Input register — fc=04 | 0 | só leitura |
| 4x0001 | Holding register — fc=03/06/16 | 0 | leitura e escrita |
| 1x0001 | Discrete input — fc=02 | 0 | só leitura |
| 0x0001 | Coil — fc=01/05/15 | 0 | leitura e escrita |

Então "4x0201" é o holding register endereço 200 no fio.

## Como integramos

- Operador lê código → impressora grava em `4x0201` (holding)
- StamPLC lê `4x0201` continuamente via Modbus TCP
- Detecta mudança → pulsa relé → liga esteira
- Confirma esteira ligada via sinal 24 V do LED
- Cronometra o tempo configurado
- Pulsa de novo → esteira para
- Envia comando `0x33` na porta 4008 → impressora apaga o código
- Operador precisa ler novo código para o próximo lote

## Autor

Ramon · [github.com/Ramon-6/CodelineSS5632](https://github.com/Ramon-6/CodelineSS5632)
