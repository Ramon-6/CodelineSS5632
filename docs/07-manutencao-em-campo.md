# Manutencao em campo (sem notebook)

Como fazer tudo no chao de fabrica usando so o M5Stack Cardputer (ou um
celular). O PLC fica isolado numa rede ponto-a-ponto com a impressora
(192.168.1.50 ↔ 192.168.1.30), sem acesso ao WiFi da fabrica. Antes de
existir esse modo manutencao, qualquer ajuste exigia levar notebook.

## Como funciona

O ESP32-S3 do StamPLC tem radio WiFi integrado alem do modulo Ethernet
(W5500 via SPI). Os dois sao hardware totalmente separado e rodam em
paralelo: o Ethernet continua falando Modbus TCP com a impressora
enquanto o WiFi sobe um pequeno Access Point proprio para manutencao.

Nenhum trafego Modbus passa pelo WiFi.

## Ligando o AP de manutencao

**No PLC**: segure o **botao A** por 3 segundos em qualquer pagina.

- O display mostra `AP LIGADO`, o SSID e o IP (192.168.4.1)
- O LED de rede WiFi comeca a aparecer no celular/Cardputer
- Fica no ar por **30 minutos** sem uso. Depois disso desliga sozinho
  (ou segure A por 3s de novo pra desligar na hora)

Credenciais padrao (muda no `config.h` antes do deploy final):

- SSID: `StamPLC-Sumi`
- Senha: `sumi1234`

## O que da pra fazer

Depois de conectado no AP, abrir no navegador:

### `http://192.168.4.1/manut`
Dashboard compacto que atualiza a cada 1,5s:

- Fase atual do ciclo (OCIOSO / OPERANDO / FIM / ERRO...)
- Codigo lido pelo scanner da impressora
- Modo (AUTO / MANUAL)
- Tempo restante do ciclo
- Rele / esteira / encoder
- IPs do PLC e da impressora
- Log cru das ultimas 40 linhas

Botoes rapidos: painel completo, log, atualizar firmware, desligar AP.

### `http://192.168.4.1/` (painel completo)
A mesma interface que roda via Ethernet — tudo que o notebook tinha
acesso. Ajustes, relatorios, eventos, config de IP, horarios da sirene,
tempos de MIX, etc.

### `http://192.168.4.1/live.json`
Snapshot JSON (polling programatico). O Cardputer pode baixar esse JSON
num loop e mostrar numa tela sua propria. Formato:

```json
{
  "fase": "OPERANDO",
  "codigo": "23.010.3062 MIX A",
  "hora_leitura": "14:32",
  "rele": 1, "esteira": 1,
  "modo": "auto",
  "sirene_hab": 0,
  "encoder": 12847,
  "encoder_fim_hab": 0,
  "tempo_alvo_s": 240,
  "tempo_restante_s": 93,
  "tempo_mixa_s": 240, "tempo_mixb_s": 480,
  "ip_plc": "192.168.1.50",
  "ip_impressora": "192.168.1.30",
  "ap_ativo": 1,
  "up_ms": 384712
}
```

### `http://192.168.4.1/log.txt`
Texto puro com as ultimas 40 linhas do log.

### `http://192.168.4.1/update`
**Atualizar firmware via OTA.** Pagina com formulario que recebe o
`firmware.bin` gerado pelo PlatformIO (`.pio/build/stamplc/firmware.bin`)
e grava direto na flash do ESP32.

Fluxo:

1. Compila firmware em casa: `pio run`
2. Copia o `firmware.bin` pro SD do Cardputer (ou pro celular)
3. Em campo, liga o AP (A 3s)
4. Abre `http://192.168.4.1/update`
5. Seleciona o arquivo e aperta Enviar
6. Upload leva ~15-30s. PLC reinicia sozinho

Durante o upload o PLC fica OFFLINE. Nao bipe codigos nem mexa na
tampa nesses 30s.

### `http://192.168.4.1/ap/off`
Desliga o AP sem precisar segurar botao.

## Plano B: Grove UART (sem WiFi)

Se o WiFi do PLC der problema (interferencia, modulo queimado) ou quiser
ter um controle offline, da pra plugar o Cardputer no conector Grove do
StamPLC. E um cabo UART (115200 8N1):

- Pino RX do Cardputer -> TX do StamPLC (GPIO 15)
- Pino TX do Cardputer -> RX do StamPLC (GPIO 13)
- GND -> GND, 5V -> 5V

(Confirme a pinagem do Grove no manual do seu modulo StamPLC —
alguns usam I2C em vez de UART na mesma porta).

Comandos aceitos (uma linha por vez, terminada por `\n`):

| Comando | Resposta |
|---|---|
| `?` ou `HELP` | lista comandos |
| `STATE` | `fase=OPERANDO encoder=12847` |
| `LIVE` | JSON completo (igual ao `/live.json`) |
| `LOG` | ultimas 40 linhas do log |
| `ENCODER` | so o numero do contador |
| `TEMPO 300` | muda tempo padrao pra 300s |
| `MODO AUTO` ou `MODO MANUAL` | troca modo |
| `TESTAR` | dispara ciclo manual |
| `PARAR` | aborta ciclo |
| `AP ON` ou `AP OFF` | liga/desliga o AP WiFi |
| `REBOOT` | reinicia o PLC |

Firmware do Cardputer sugerido: um sketch que mostra `LIVE` a cada 1s na
tela, mapeia 1 tecla de atalho pra `TESTAR`, outra pra `PARAR`, outra pra
`AP ON`. ~80 linhas de Arduino.

## Fluxo tipico: bug no chao de fabrica

1. Ramon chega na fabrica, PLC apresenta algo estranho
2. Segura A por 3s -> AP sobe
3. Cardputer conecta, abre `/manut` -> ve o log cru + estado
4. Baixa `/log.txt` pro SD do Cardputer e manda pro Claude
5. Claude analisa e devolve `firmware.bin` corrigido
6. Ramon copia `.bin` pro SD do Cardputer
7. Em `/update` sobe o arquivo. PLC reinicia em 15s
8. Testa a correcao, confirma. Desliga o AP (A 3s ou `/ap/off`)

**Zero notebook. Zero cabo de rede novo. Zero mexer na impressora.**

## Seguranca

- O AP so sobe quando acionado pelo botao fisico — nao fica aberto
  o tempo todo
- Timeout automatico de 30 min
- Senha WPA2 (padrao `sumi1234`, troque antes do deploy final em
  `config.h`)
- O OTA valida assinatura basica do firmware (ESP-IDF Update class)
  e aborta se o `.bin` nao for valido pro ESP32-S3
- Durante o OTA o watchdog e desligado automaticamente pela classe
  `Update`; se der ruim no meio, o bootloader volta pra particao
  anterior no proximo boot

## Se der algo errado durante o OTA

- Upload interrompido (perdeu wifi, bateria do Cardputer): o PLC
  descarta o firmware incompleto e volta a rodar o antigo. Pode tentar
  de novo
- `.bin` corrompido: a flash grava, mas no boot o bootloader detecta
  checksum invalido e reverte pra particao anterior automaticamente
- Firmware novo com bug fatal (travamento no setup): o watchdog
  reseta, e como a particao anterior ainda existe o PLC pode voltar
  pra ela com um reset longo (10s desligado antes de religar)

## Mudar SSID/senha depois do deploy

Edite `src/config.h`:

```c
#define MANUTENCAO_AP_SSID   "StamPLC-NomeDaMaquina"
#define MANUTENCAO_AP_SENHA  "umaSenhaMelhor"
```

Recompila, sobe o `.bin` novo pelo proprio `/update`. Depois disso o AP
usa o SSID novo.
