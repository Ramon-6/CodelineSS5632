# StamPLC Manut — firmware do Cardputer

Firmware do M5Stack Cardputer que vira **controle remoto do PLC em
campo**. Conecta no AP WiFi que o StamPLC sobe quando o Ramon segura
o botão A por 3s, e deixa:

- Ver status do ciclo ao vivo (fase, código, tempo, encoder)
- Mudar **todos os tempos** (padrão, MIX A, MIX B, espera)
- Trocar modo AUTO/MANUAL
- Ligar/desligar sirene e horários
- **Falar Modbus direto na impressora** (ler/escrever qualquer registro)
- Puxar o log cru
- Disparar ou parar ciclo de teste

Nada disso precisa de notebook.

## Como compilar

```bash
cd cardputer-manut
pio run -t upload
```

Precisa do PlatformIO instalado. A board é `m5stack-stamps3` (o Cardputer
usa o mesmo ESP32-S3). Bibliotecas vão sozinhas pelo `platformio.ini`:

- `M5Cardputer`
- `M5Unified` + `M5GFX`
- `ArduinoJson`

## Como usar

### Primeira vez

1. No PLC segura **A por 3s** até aparecer `AP LIGADO`
2. Liga o Cardputer. Vai aparecer o menu principal
3. Navega até o ícone **WIFI** com `.` e `,` (vírgula/ponto)
4. **Enter** pra entrar
5. Seleciona `CONECTAR` com `;` e `/` (ponto-e-vírgula/barra), **Enter**
6. Em 2-3s mostra `CONECTADO` + IP

Se o AP tiver outro SSID/senha (você mudou no `config.h`), use
`TROCAR REDE` dentro do menu WIFI. Fica salvo no NVS do Cardputer.

### Teclado

| Tecla | O que faz |
|---|---|
| `,` ou `<` | esquerda / anterior |
| `.` ou `>` | direita / próximo |
| `;` | cima |
| `/` | baixo |
| **Enter** | entrar / aplicar |
| **`** (crase) | voltar |
| **Backspace** | apagar na edição de texto |
| Letras/dígitos | digitação direta no SSID/senha e no Modbus |

### Menu principal

6 ícones: **WIFI, STATUS, AJUSTES, MODBUS, LOG, SOBRE**

- **STATUS**: polling de `/live.json` a cada 1,5s. Fase do ciclo
  destacada em cor (verde=ocioso, laranja=operando, vermelho=erro).
- **AJUSTES**: lista com todos os parâmetros editáveis. Enter abre o
  editor — `. ,` muda ±10, `; /` muda ±60 (números) ou toggle (bool).
  Enter aplica via `/set?...`
- **MODBUS**: formulário FC+Addr+N. `; /` troca o campo, `. ,`
  incrementa/decrementa. Enter executa `/mb?...` e mostra JSON.
- **LOG**: puxa `/log.txt`. `; /` scrolla. Enter atualiza.

### Fluxo típico de debug

1. Operador reporta bug: "a máquina está parando sozinha"
2. No PLC: segura A por 3s
3. Cardputer já emparelhado → entra no `STATUS` → vê a fase atual
4. Entra no `LOG` → scrolla pra ver o que o PLC registrou nos últimos
   minutos
5. Entra no `MODBUS` → `fc=3 a=200 n=25` lê o scanner → confirma se
   o código ainda está lá
6. Entra no `MODBUS` → `fc=2 a=17 n=1` lê o fim de curso da tampa
7. Entende o problema → entra em `AJUSTES` → ajusta o tempo que estiver
   errado, OU vai à Web via browser do celular pra atualizar firmware
   em `http://192.168.4.1/update`

Zero notebook.

## Design

- Fundo branco, letra preta (reduz reflexo em iluminação de fábrica)
- Ícones vetoriais simples (sem bitmap pra não gastar flash)
- Fontes default do M5GFX

## Repo

[github.com/Ramon-6/CodelineSS5632](https://github.com/Ramon-6/CodelineSS5632)
