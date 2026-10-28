#pragma once

// ---- Rede ------------------------------------------------------------
// IP fixo do StamPLC e da impressora. Os dois precisam estar na mesma faixa.
#define STAMPLC_IP      192, 168, 1, 50
#define STAMPLC_GATEWAY 192, 168, 1, 1
#define STAMPLC_MASK    255, 255, 255, 0
#define IMPRESSORA_IP   192, 168, 1, 100   // <-- trocar pelo IP real da impressora
#define IMPRESSORA_PORTA 1502              // porta Modbus TCP (manual)
#define MODBUS_UNIT     1

// ---- Automacao -------------------------------------------------------
#define RELE_CANAL      0        // 0..3 (Relay 1..4 do StamPLC) - esteira
#define RELE_TEMPO_MS   480000   // tempo padrao da esteira (8 min) - alteravel na tela
#define POLL_MS         100      // intervalo de leitura da impressora

// O que conta como "operador leu o codigo":
//   TRIGGER_TEXTO  - o dado seguinte (3x0201) passou a ter um texto novo
//   TRIGGER_CACHE  - a quantidade de dados em cache (3x0002) aumentou
// Os dois ligados = qualquer um dispara. Ajustar depois do teste na fabrica.
#define TRIGGER_TEXTO   1
#define TRIGGER_CACHE   1

// Como apagar o codigo visivel na impressora no fim do ciclo:
//   0 = Modbus, bobina 0x0002 (NAO limpa o codigo do leitor USB)
//   1 = protocolo privado, porta 4008, comando 0x10 (clear cache)
//   2 = porta 4008, comando 0x33 (reload template - APAGA O CODIGO DO LEITOR) <- usar este
#define LIMPAR_METODO   2
#define LIMPAR_TENTATIVAS 3
#define TEMPLATE_NOME   "sumi rico"  // nome do template atual na impressora

// ---- Esteira (referencia do LED 24V do botao pulsante) -------------------
// 0 = IN1 do StamPLC (recebe o 24V do LED do botao: aceso = esteira rodando)
// -1 = (modo bancada, sem IN1) considera esteira ligada quando o rele esta fechado
#define ESTEIRA_ENTRADA     0
#define ESTEIRA_ATIVO_ALTO  1   // 1 = entrada com tensao (24V) significa esteira ligada

// Tolerancia (ms) para o LED cair depois do pulso de parar. Antes era 2000 - era
// pouco: a esteira para mas o LED segura, e o firmware ia pra PARA_INSISTE achando
// que travou. 5000 = mais honesto com o tempo real do contator.
#define TOLERANCIA_PARAR_MS 5000

// Depois do boot, ignora qualquer disparo por este tempo. Resolve "StamPLC desliga
// e liga -> ja inicia ciclo" (codigo residual na impressora era interpretado como
// leitura nova).
#define BOOT_GRACE_MS       10000       // 10s - dar tempo pra scanner USB e Modbus estabilizar
// E ao conectar na impressora, apaga qualquer codigo residual (manda 0x33). Deixe
// em 0 se tiver medo de perder um codigo valido logo apos reset.
#define APAGA_AO_CONECTAR   1

// Teto global: estados intermediarios (PARA_INSISTE, PARA_SEM_CODIGO) nao podem
// durar mais que isso. Depois cai em ERRO que apaga codigo e volta pro PRONTO.
#define TEMPO_MAX_FASE_MS   120000  // 2 min
// Reset automatico depois do ERRO: apaga codigo e volta pro OCIOSO para o
// operador ser forcado a ler um codigo novo. Operador NAO tem acesso ao PLC,
// entao erro nao pode travar producao.
#define ERRO_RESET_MS       5000
// Limpeza do codigo: numero maximo de tentativas antes de desistir e forcar OCIOSO.
// A impressora pode estar offline/travada; o importante e nao prender o PLC.
#define LIMPANDO_MAX_TENTATIVAS 20
// Debug: envia pela serial toda mudanca de registro Modbus. Desligar em producao.
#define DEBUG_SCANNER_MODBUS 0

// ---- Receitas MIX A / MIX B (ponto 3) ---------------------------------
// Texto lido no scanner vem tipo: "23.010.3062 MIX A". Detectamos "MIX A" ou
// "MIX B" (case-insensitive) e usamos o tempo correspondente. Sem MIX = usa
// RELE_TEMPO_MS normal.
#define MIX_RECIPE_HABILITADO  1
#define TEMPO_MIX_A_MS         240000    // 4 min
#define TEMPO_MIX_B_MS         480000    // 8 min

// ---- Modo manual/automatico (ponto 5) ---------------------------------
// AUTO (padrao): PLC controla relé conforme leitura da impressora.
// MANUAL: PLC ignora leitura; pulso no IN2 pulsa o relé por PULSO_MS.
// Troca so pela tela do PLC (botao segurado na pagina ESTADOS).
#define MODO_MANUAL_HABILITADO 1
#define MODO_MANUAL_BOTAO_IN   1        // IN2 (0=IN1, 1=IN2 ...)

// ---- Sirene limpeza do cabecote (ponto 4) -----------------------------
// Em cada horario, liga R2 ate operador abrir a tampa (1x0018=1) e fechar
// (volta a 0). Deixe HABILITADO=0 ate ajustar os horarios no painel.
#define SIRENE_LIMPEZA_HABILITADO 1       // habilitado por padrao com bloqueio ativo
#define SIRENE_CANAL              1       // R2
#define SIRENE_HORARIOS_PADRAO    "07:00,16:00,00:00"
// Tempo minimo que a tampa precisa ficar aberta pra considerar "limpou de verdade".
// Protege contra operador que so encosta no fim de curso.
#define SIRENE_TAMPA_MIN_ABERTA_MS 5000   // 5 s

// ---- Manutencao em campo: AP WiFi + OTA (ponto 7) ---------------------
// Botao A segurado 3s em qualquer pagina do PLC sobe um Access Point WiFi
// para o Cardputer/celular acessar o painel + atualizar firmware sem notebook.
// NAO INTERFERE no Ethernet que fala com a impressora (hardware separado).
#define MANUTENCAO_AP_SSID       "Batoff"
#define MANUTENCAO_AP_SENHA      "batoff1234"      // minimo 8 chars
#define MANUTENCAO_AP_TIMEOUT_MS 1800000UL         // 30 min sem cliente -> desliga

// ---- Grove UART: Cardputer conectado por cabo (plano B offline) -------
// Protocolo texto, 1 linha por comando. Pinos padrao do conector Grove do
// StamPLC (confirmar se o modulo tiver outra pinagem).
#define GROVE_SERIAL_HABILITADO  1
#define GROVE_SERIAL_PIN_RX      13
#define GROVE_SERIAL_PIN_TX      15
#define GROVE_SERIAL_BAUD        115200

// ---- Encoder/contador 4x0061 (ponto 6) --------------------------------
// Fase 1 (atual): so loga no CSV o valor do contador a cada N segundos.
// Depois de analisar o grafico, ativar FIM_HABILITADO=1 pra que o fim do
// ciclo seja quando o contador para de incrementar por PARADO_MS.
#define ENCODER_FIM_HABILITADO    0
#define ENCODER_PARADO_MS         60000   // 1 min sem incrementar = acabou borracha
#define ENCODER_AQUECIMENTO_MS    30000   // ignora os primeiros 30s do ciclo
#define ENCODER_LOG_INTERVAL_S    10      // frequencia de log do contador
