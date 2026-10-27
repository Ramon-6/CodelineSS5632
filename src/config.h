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
#define RELE_TEMPO_MS   600000   // tempo padrao da esteira rodando (10 min) - alteravel na tela
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
#define BOOT_GRACE_MS       5000
// E ao conectar na impressora, apaga qualquer codigo residual (manda 0x33). Deixe
// em 0 se tiver medo de perder um codigo valido logo apos reset.
#define APAGA_AO_CONECTAR   1

// ---- Receitas MIX A / MIX B (ponto 3) ---------------------------------
// Texto lido no scanner vem tipo: "23.010.3062 MIX A". Detectamos "MIX A" ou
// "MIX B" (case-insensitive) e usamos o tempo correspondente. Sem MIX = usa
// RELE_TEMPO_MS normal.
#define MIX_RECIPE_HABILITADO  1
#define TEMPO_MIX_A_MS         600000    // 10 min
#define TEMPO_MIX_B_MS         900000    // 15 min

// ---- Modo manual/automatico (ponto 5) ---------------------------------
// AUTO (padrao): PLC controla relé conforme leitura da impressora.
// MANUAL: PLC ignora leitura; pulso no IN2 pulsa o relé por PULSO_MS.
// Troca so pela tela do PLC (botao segurado na pagina ESTADOS).
#define MODO_MANUAL_HABILITADO 1
#define MODO_MANUAL_BOTAO_IN   1        // IN2 (0=IN1, 1=IN2 ...)

// ---- Sirene limpeza do cabecote (ponto 4) -----------------------------
// Em cada horario, liga R2 ate operador abrir a tampa (1x0018=1) e fechar
// (volta a 0). Deixe HABILITADO=0 ate ajustar os horarios no painel.
#define SIRENE_LIMPEZA_HABILITADO 0
#define SIRENE_CANAL              1       // R2
#define SIRENE_HORARIOS_PADRAO    "08:00,13:00,18:00"

// ---- Encoder/contador 4x0061 (ponto 6) --------------------------------
// Fase 1 (atual): so loga no CSV o valor do contador a cada N segundos.
// Depois de analisar o grafico, ativar FIM_HABILITADO=1 pra que o fim do
// ciclo seja quando o contador para de incrementar por PARADO_MS.
#define ENCODER_FIM_HABILITADO    0
#define ENCODER_PARADO_MS         60000   // 1 min sem incrementar = acabou borracha
#define ENCODER_AQUECIMENTO_MS    30000   // ignora os primeiros 30s do ciclo
#define ENCODER_LOG_INTERVAL_S    10      // frequencia de log do contador
