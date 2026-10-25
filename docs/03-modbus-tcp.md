# Modbus TCP (porta 1502)

Mapa completo dos registros Modbus da impressora, com observação do que
é verdade na prática e o que o manual descreve errado.

Convenção: `4x0201` significa holding register 201 em notação Modicon
antiga = endereço 200 na transmissão (base-0, fc=03/06/16).

## Coils (0xXXXX) — fc=01 (ler) / fc=05 (escrever)

| Endereço | Função | Observação prática |
|---|---|---|
| 0x0001 | Start/stop print — 1=para, 0=inicia | OK |
| **0x0002** | **Clear network data cache** | **NÃO apaga código do leitor USB** — só limpa o cache interno de dados de rede |
| 0x0003 | Network data cache mode (1=enable) | **Precisa estar em 1** para o cache funcionar — nosso firmware habilita no boot |
| 0x0004 | Clean when jet on/off | OK |
| 0x0005 | Jet on/off | Trabalha junto com 0x0004 |
| 0x0006 | Fill ink into mixing tank | OK |
| 0x0007 | Fill ink failure alarm | Pode escrever 0 para reset |
| 0x0009 | Purge | 1=purging |
| 0x0010 | Measure viscosity | OK |
| 0x00011 | Add solvent | OK |
| 0x00012 | Clean nozzle | OK |
| 0x00016 | Single overlay printing | 1=atualiza assim que recebe |
| 0x00017 | Circular printing | 1=imprime circular |
| **0x0019** | **Write SN enable bit** | Habilita a escrita do SN (4x1001). Nosso firmware liga no boot |

## Discrete inputs (1xXXXX) — fc=02

### Flags gerais

| Endereço | Função |
|---|---|
| 1x0001 | Flag de alarme (qualquer alarme ativo) |
| 1x0100 | Printer ready (1=pronto) |

### Alarmes específicos (1x0002 a 1x0053)

| Endereço | Alarme |
|---|---|
| 1x0002 | Pressão alta |
| 1x0003 | Velocidade da bomba muito alta |
| 1x0004 | Temperatura ambiente muito alta |
| 1x0005 | Temperatura elétrica muito alta |
| 1x0006 | Vida útil do filtro < 200 horas |
| 1x0007 | Vida útil do filtro < 100 horas |
| 1x0008 | Vida útil do filtro < 48 horas |
| 1x0009 | Falha da calha (gutter) |
| 1x0010 | Falha do heater |
| 1x0011 | Cartucho de tinta vazio |
| 1x0012 | Cartucho de solvente vazio |
| 1x0013 | Falha de viscosidade |
| 1x0014 | Viscosidade excede setpoint (>10) |
| 1x0015 | Falha ao encher tanque de mistura |
| 1x0016 | Nível do tanque de mistura baixo |
| 1x0017 | Tanque de mistura transbordando |
| 1x0018 | Cabeça de impressão aberta |
| 1x0019 | Falha de alta tensão |
| 1x0020 | Temperatura ambiente muito baixa |
| 1x0021-1x0029 | Problemas com cartucho de tinta (RFID, autenticação, modelo errado, expirado, etc) |
| 1x0030-1x0038 | Mesmo para o cartucho de solvente |
| 1x0039-1x0047 | Mesmo para o filtro |
| 1x0048 | Falha na síntese do QR code |
| 1x0049-1x0052 | Chip de capacidade do cartucho (série K76) |
| 1x0053 | Viscosidade muito baixa |

## Input registers (3xXXXX) — fc=04

| Endereço | Função | Observação |
|---|---|---|
| 3x0001 | Tamanho do cache da impressora | OK |
| 3x0002 | **Quantidade atual no cache** | **Fica 0 quando o leitor USB preenche** — o cache de rede é separado do template do leitor |
| 3x0003 | Nível do tanque de mistura (1-4) | OK |
| 3x0004 | % de tinta restante | OK |
| 3x0005 | % de solvente restante | OK |
| 3x0006 | Viscosidade da tinta | Flutua naturalmente |
| 3x0007 | Pressão da tinta | * 0,0018 = valor na tela |
| 3x0008 | Velocidade da bomba de engrenagens | OK |
| 3x0009 | Estado da alta tensão (0=off, 1=on) | OK |
| 3x0010 | Estado da tampa da cabeça (0=fechada, 1=aberta) | OK |
| 3x0011 | Estado do cartucho de solvente (0=OK, 1=erro) | OK |
| 3x0012 | Estado do cartucho de tinta (0=OK, 1=erro) | OK |
| 3x0013 | Estado da calha (0=OK, 1=erro) | OK |
| 3x0014 | Temperatura da tinta | Flutua |
| 3x0015 | Temperatura elétrica | Flutua |
| 3x0016 | Temperatura da cabeça | Flutua |
| 3x0017 | Temperatura ambiente | Flutua |
| 3x0018 | Tempo restante do filtro (horas) | OK |
| 3x0019 | Tempo de operação (Dword) | AB CD, 32 bits |
| 3x0021 | Estado da máquina | 0=jet on, 1=jet off, 2=running, 3=stopped, 5=fill failed, 6=filling |
| 3x0031-3x0049 | Modelo do cartucho de tinta (ASCII) | OK |
| 3x0050-3x0055 | Datas e validade do cartucho de tinta | OK |
| 3x0061-3x0085 | Mesmo para o cartucho de solvente | OK |
| 3x0091-3x0114 | Mesmo para o filtro principal | OK |
| **3x0201-3x0249** | **"Next network data to be printed"** | **Enganoso no manual** — só é preenchido quando dados chegam por Modbus. **O leitor USB NÃO escreve aqui**. Use 4x0201 (holding) em vez |
| 3x0501-3x0510 | Modelo da máquina (ASCII) | OK |
| 3x0511-3x0530 | Versão APP (ASCII) | OK |
| 3x0531-3x0546 | Versão kernel (ASCII) | OK |
| 3x0547-3x0562 | Versão driver (ASCII) | OK |
| 3x0563-3x0578 | Versão printhead (ASCII) | OK |
| 3x0579-3x0594 | Versão CPLD (ASCII) | OK |
| 3x0595-3x0610 | Versão Phase (ASCII) | OK |

## Holding registers (4xXXXX) — fc=03 (ler) / fc=06/16 (escrever)

### Configuração de impressão

| Endereço | Função | Faixa |
|---|---|---|
| 4x0001 | Print mode | 0=photocell, 1=encoder, 2=photocell auto, 3=auto |
| 4x0002 | Raster mode | 0..15 (vários formatos) |
| 4x0003 | Print direction | 0=regular, 1=reverse, 2=inverse, 3=inv+rev |
| 4x0004 | Traverse print | 0=disable, ±1..4=modo |
| 4x0005 | Print delay | 0..60000 |
| 4x0006 | Print speed | 0..1000 |
| 4x0007 | Bold | 0..100 |
| 4x0008 | Qtd de traverse prints | 0..100 (se software trigger) |
| 4x0009 | Meter factor | 0..60000 |
| 4x0010 | Prints por trigger | 1..255 |
| 4x0011 | Photocell delay | 0..59999 |
| 4x0012 | Trigger edge | 0=trailing, 1=leading |
| 4x0013 | Charge mode | 0=standard, 1=high speed |

### Configuração de tinta

| Endereço | Função | Faixa |
|---|---|---|
| 4x0031 | Viscosity setpoint | 30..100 |
| 4x0032 | Pressure setpoint | 0..450 |
| 4x0033 | Printhead heating switch | 0/1 |
| 4x0034 | High voltage switch | 0/1 |
| 4x0035 | Modulation setpoint | 0..100 |
| 4x0036 | Height | 0..100 |
| 4x0037 | Delay multiplier | 0=×1, 1=×2, 2=×4, 3=×8 |
| 4x0038 | Encoder divider | 0..4 |
| 4x0039 | BCD setting | 0=disable, 1..3=bits |

### Relógio e contadores

| Endereço | Função |
|---|---|
| 4x0059-4x0060 | System time (Dword, Long AB CD) |
| 4x0061-4x0062 | **Product counter (Dword)** — total de impressões reais |
| 4x0063-4x0064 | Counter 1 start |
| 4x0065-4x0066 | Counter 1 stop |
| 4x0067-4x0068 | Counter 1 current |
| 4x0069-4x0070 | Counter 2 start |
| 4x0071-4x0072 | Counter 2 stop |
| 4x0073-4x0074 | Counter 2 current |

**O contador 4x0061** só incrementa quando a impressora **realmente
imprime** (produto passa pela fotocélula). Não serve como gatilho
para detectar leitura do scanner, só como confirmação de impressão.

### Dados de rede e scanner

| Endereço | Função | Observação |
|---|---|---|
| 4x0200 | **Flag — tem código?** | 0 = vazio, 0xFFFF = tem código. **Descoberto na prática, não está no manual** |
| **4x0201-4x0220** | **Código lido pelo leitor USB (ASCII, terminador 0x09)** | **Este é o campo real do scanner**. O manual chama de "Network data transmit register" mas o leitor também escreve aqui |
| 4x1001-4x1014 | Read & write SN (14 chars ASCII) | Precisa coil 0x0019 habilitado |

### Como o leitor USB preenche

Quando o operador escaneia "7908417101231":

```
4x0200 = 0xFFFF          ← flag "tem código"
4x0201 = 0x37            ← '7'
4x0202 = 0x39            ← '9'
4x0203 = 0x30            ← '0'
4x0204 = 0x38            ← '8'
4x0205 = 0x34            ← '4'
4x0206 = 0x31            ← '1'
4x0207 = 0x37            ← '7'
4x0208 = 0x31            ← '1'
4x0209 = 0x30            ← '0'
4x0210 = 0x31            ← '1'
4x0211 = 0x32            ← '2'
4x0212 = 0x33            ← '3'
4x0213 = 0x31            ← '1'
4x0214 = 0x09            ← terminador
4x0215 em diante = 0
```

**Como ler no firmware (StamPLC):**

```cpp
uint16_t r[16];
lerRegs(3, 199, 16, r);  // fc=03, addr 199, 16 regs
// r[0] = flag (4x0200), r[1..] = texto (4x0201..)
char texto[20] = "";
int n = 0;
for (int i = 1; i < 16 && n < 19; i++) {
    char c = r[i] & 0xFF;
    if (c == 0x09 || c == 0) break;
    if (c >= 32 && c < 127) texto[n++] = c;
}
texto[n] = 0;
```
