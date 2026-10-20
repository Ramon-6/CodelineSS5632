# Impressora + StamPLC (Sumiriko)

Automacao para impressora de tinta que marca lotes em fita de borracha.
Quando o operador le o codigo de barras no leitor USB da impressora, o
StamPLC (M5Stack) pulsa um rele em paralelo com o botao da esteira, cronometra
o tempo configurado, pulsa de novo para parar e apaga o codigo da impressora.

## Hardware

- **M5Stack StamPLC** (ESP32-S3, 4 reles, 8 entradas isoladas 5-36V)
- **Modulo PoE W5500** para rede Ethernet
- Fonte **12-24V DC** nos terminais de alimentacao (sem ela os reles nao acionam)

## Ligacoes fisicas

- **R1 (COM + NA)** em paralelo com o botao pulsante da esteira.
- **IN1** recebe o **24V do LED do botao** (24V = esteira rodando).
- **PoE/RJ45** no mesmo switch da impressora (Modbus TCP porta 1502).

## Ciclo

1. Operador le o codigo no leitor USB da impressora.
2. StamPLC detecta via Modbus (mudanca de texto ou cache).
3. Se a esteira ja estava rodando: pulsa para parar, espera parar, segue.
4. Pulsa R1 (300 ms) para LIGAR a esteira.
5. Confirma via IN1 que esteira ligou.
6. Cronometra o tempo configurado (padrao 10 s, alteravel na tela e na web).
7. Pulsa R1 para PARAR.
8. Confirma via IN1 que esteira parou.
9. Manda a impressora apagar o codigo (bobina 0x0002 do Modbus).

Em qualquer erro (esteira nao ligou, parou sozinha, etc) o StamPLC avisa
na tela, grava no relatorio e aguarda o operador apertar C para cancelar.

## Interfaces

### Tela do StamPLC (5 paginas, botao A navega)

1. **PAINEL** - status atual, ultimo codigo, totais do dia, reles
2. **RELES** - estado dos 4 reles e tensao de alimentacao
3. **ESTADOS** - rede, impressora, esteira, SD, hora, alarme, 24V
4. **EVENTOS** - ultimas acoes
5. **AJUSTES** - tempo da esteira (B = +1s, C = -1s)

Durante o ciclo a navegacao e substituida por telas full-screen coloridas
(amarelo = em andamento, verde = rodando com cronometro, azul = apagando,
vermelho = erro).

### Pagina web (http://IP_DO_STAMPLC)

Mesmo menu: Painel, Hoje, Dias anteriores, Eventos, Ajustes.
Atualiza sozinha e permite baixar o CSV do dia para o Excel.

## Relatorio

Um arquivo CSV por dia no cartao SD (`/rel/AAAA-MM-DD.csv`), com codigos lidos,
tempo de esteira ligada, tempo parada, falhas e eventos tecnicos.

## Build

```
pio run              # compila
pio run -t upload    # grava via USB-C (porta COM)
```

Precisa do PlatformIO instalado.
