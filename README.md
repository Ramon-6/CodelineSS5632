# Automação Sumiriko — Impressora + StamPLC

Integração entre uma impressora de inkjet industrial Codeline SS5632 e um
StamPLC (M5Stack). Quando o operador lê o código de barras no leitor USB
da impressora, o PLC detecta a leitura por Modbus TCP, pulsa um relé para
ligar a esteira, cronometra o tempo configurado e no fim pulsa de novo
para parar. No final apaga o código da impressora para forçar o operador
a ler um novo antes do próximo lote.
<img width="1000" height="824" alt="IMG_8380-web" src="https://github.com/user-attachments/assets/0beac44a-3027-48e0-895f-a7b694e67685" />

## Hardware

- **M5Stack StamPLC** (ESP32-S3, 4 relés 5 A, 8 entradas isoladas 5-36 V)
- **Módulo PoE W5500** para rede Ethernet
- **Fonte 12-24 V DC** nos terminais V+ / V− do StamPLC
- **Impressora Codeline SS5632** (base de dados em cima do protocolo
  "FKK-5.1.13.0", disponibilizado via Modbus TCP + protocolo privado
  nas portas 4008 e 4040)
<img width="221" height="177" alt="Captura de tela 2026-10-25 134256" src="https://github.com/user-attachments/assets/17d0a32a-965d-4e97-8897-01a833d0f439" />

## Ligações físicas

```
  LED do botão pulsante da esteira (+24 V quando ligada)
            │
            └─ IN1 do StamPLC
            
  GND do CLP da máquina
            │
            └─ COM das entradas do StamPLC

  Botão pulsante da esteira (dois terminais)
            │
            ├── COM do R1 do StamPLC
            └── NA do R1 do StamPLC
            
  (COM + NA ficam em paralelo com o botão — o relé "aperta" por software)
```

## Como funciona a comunicação com a impressora

A impressora expõe três serviços na rede:

| Porta | Protocolo | Usamos para |
|-------|-----------|-------------|
| 1502 | Modbus TCP | Ler o código, status, contador |
| 4008 | Privado (0x1B 0x1F ...) | Apagar o código (reload template) |
| 4040 | Privado | A impressora usa internamente para o leitor USB (não dá para espionar de fora) |

### Mapa Modbus que usamos

| Endereço | Função | Significado |
|----------|--------|-------------|
| 4x0201 (holding, fc=03, addr 200, 15 regs) | Ler | **Código lido pelo leitor USB** |
| 4x0061 (holding, fc=03, addr 60, 2 regs) | Ler | Total de impressões (contador) |
| 3x0002 (input, fc=04, addr 1) | Ler | Cache de dados de rede (não usamos como gatilho) |
| 3x0021 (input, fc=04, addr 20) | Ler | Estado da máquina |
| 1x0001 (discrete, fc=02, addr 0) | Ler | Flag de alarme |
| 0x0003 (coil, fc=05, addr 2) | Escrever | Habilitar modo cache (ligamos no boot) |
| 0x0019 (coil, fc=05, addr 24) | Escrever | Habilitar escrita de SN (ligamos no boot) |

### Comando de apagar código (porta 4008)

```
Hex: 1B 1F [len_lo] [len_hi] 33 [nome_do_template] 00 1C 17
```

Enviamos o comando **0x33** (force reload template) com o nome do
template atual (ex: "sumi rico"). A impressora recarrega o template
zerado, apagando qualquer código que o leitor tinha inserido.

Importante: o comando **0x43** (open template) do manual **não serve** —
ele só reload se o template não estiver aberto, e como ele está sempre
aberto, o 0x43 não apaga nada.

## Lições que a documentação não conta

Guardadas aqui para o próximo que mexer nessa família de impressora.

### 1. O leitor USB NÃO escreve no 3x0201 (input register)

O manual Modbus mostra `3x0201 — the next network data to be printed`
como um input register, mas isso **só é preenchido quando os dados
chegam pela rede (Modbus write)**. Quando o leitor USB preenche o
template, o código vai para `4x0201` (holding register, bidirecional)
— `fc=03`, não `fc=04`.

Levamos horas descobrindo isso. Sempre leia o 4x0201 (holding), não o
3x0201 (input).

### 2. A impressora não notifica sobre leituras do scanner

Só manda notificação de **impressão real** (comando 0x16 na porta 4008),
que depende do encoder girando. Para detectar leitura do scanner, só
monitorando o 4x0201 continuamente.

### 3. O W5500 do StamPLC tem socket exhaustion

Com 8 sockets totais, dois servidores TCP (web + outro) + cliente
Modbus persistente **esgotam os sockets** e o cliente Modbus para de
conseguir abrir conexão nova. Sintoma: `mb.connect()` sempre retorna 0,
mesmo com rede perfeita.

Solução que ficou:
- Um só servidor TCP (web na porta 80)
- Cliente Modbus persistente que mantém a conexão aberta entre
  transações. Se cair, só aí reconecta.

### 4. Protocolo Serial x Ethernet são exclusivos

Na tela da impressora, em "Serial Port Setting" o campo "Protocolo"
tem opções Modbus RTU / 4040 / 4002 / 8017 / None. Se colocar
qualquer coisa que não seja **None**, a serial é ligada E a Ethernet
é desabilitada (incluindo o Modbus TCP na 1502). O ping continua
funcionando mas o 1502 fica fechado.

Para o nosso projeto, deixe sempre em **None** nessa tela.

### 5. A impressora aceita só 1 cliente externo na 4040

A porta 4040 é usada internamente pela impressora para o leitor USB
(aparece como "Conectado: 127.0.0.1" nos logs dela). Qualquer tentativa
de conectar do PLC na 4040 externamente é recusada. Nada a fazer.

### 6. Reiniciar a fonte do PLC é diferente de software reset

O W5500 só reseta de verdade quando perde energia. Software reset do
ESP32 (`ESP.restart()`) mantém o W5500 vivo com o mesmo estado. Se o
cliente Modbus trava, desligar a fonte 12 V por 30 segundos resolve.

### 7. Pulso curto demais no botão pulsante

Um "clique humano" dura cerca de 300 a 500 ms. Testamos 300 ms e o
botão da esteira não registrou. **Ficou em 1000 ms** (1 segundo), que
funcionou. Se o botão da sua esteira tiver debounce agressivo, aumente
mais.

### 8. Polling de 1,5 s é muito lento para o operador

O polling padrão de 1,5 segundos deu uma sensação de atraso perceptível
entre ler o código e a esteira ligar. Reduzimos para 150 ms com
prioridade na leitura do 4x0201 (texto). Latência de leitura ficou em
~300 ms a 1 s.

### 9. Erro no ciclo precisa auto-recuperar

Se a esteira não liga/não para dentro do timeout, o PLC entra em ERRO.
Se o painel do PLC fica fechado (sem acesso ao botão C), fica travado.

Nossa solução: após 10 s em ERRO, o PLC apaga o código automaticamente
e volta para OCIOSO. Operador só precisa ler novo código.

## Interfaces

### Tela do StamPLC (5 páginas, botão A navega)

1. **PAINEL** — status atual grande, último código, totais do dia
2. **RELÉS** — estado dos 4 relés + tensão da fonte
3. **ESTADOS** — rede, impressora, esteira, SD, hora, alarme
4. **EVENTOS** — últimas ações
5. **AJUSTES** — tempo da esteira rodando (B = +1 min, C = −1 min)

Durante o ciclo a navegação é substituída por telas full-screen:
- **Amarelo**: LIGANDO / PARANDO / AJUSTANDO
- **Verde**: ESTEIRA RODANDO (com cronômetro MM:SS)
- **Azul**: APAGANDO CÓDIGO
- **Vermelho**: ERRO (volta sozinho em 10 s)

### Página web (http://IP_DO_STAMPLC)

Mesmo menu: Painel, Hoje, Dias anteriores, Eventos, Ajustes. Pode editar
IPs, tempo e origem da esteira sem recompilar firmware. Os relatórios do
dia podem ser baixados em CSV.

## Fluxo completo

1. Operador lê o código de barras no leitor USB da impressora
2. PLC detecta mudança no `4x0201` (menos de 1 s)
3. PLC pulsa R1 por 1 s → esteira começa a ligar
4. PLC confirma esteira ligada via +24 V na IN1
5. Cronometra o tempo configurado (padrão 10 min, ajustável na tela)
6. PLC pulsa R1 por 1 s → esteira começa a parar
7. PLC confirma esteira parada (IN1 em 0 V)
8. PLC envia comando 0x33 na porta 4008 da impressora
9. Impressora recarrega o template, apagando o código
10. Operador precisa ler novo código para o próximo lote

**Em caso de erro** (esteira não respondeu em 2 s):
- Tela fica vermelha "ERRO NO CICLO"
- Após 10 s o PLC apaga o código automaticamente
- Volta para OCIOSO esperando novo código

## Relatório

Um arquivo CSV por dia no cartão SD do PLC (`/rel/AAAA-MM-DD.csv`), com
códigos lidos, tempo de esteira ligada, tempo parada, falhas e eventos
técnicos. Visualização pela aba Hoje / Dias anteriores da web.

## Compilação

```
pio run              # compila
pio run -t upload    # grava via USB-C
```

Precisa do PlatformIO instalado. O firmware usa:
- M5StamPLC
- M5-Ethernet
- M5Unified
- M5GFX

Ajustes padrão em `src/config.h`:
- IP do PLC: 192.168.1.50
- IP da impressora: 192.168.1.30
- Porta Modbus: 1502
- Pulso do botão: 1 segundo
- Tempo esteira: 10 minutos
- Entrada da esteira: IN1
- Nome do template: "sumi rico" (muda se o arquivo da impressora mudar)
