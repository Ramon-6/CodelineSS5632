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
#define RELE_CANAL      0        // 0..3 (Relay 1..4 do StamPLC)
#define RELE_TEMPO_MS   10000    // tempo padrao da esteira rodando (10 s) - alteravel na tela
#define POLL_MS         100      // intervalo de leitura da impressora

// O que conta como "operador leu o codigo":
//   TRIGGER_TEXTO  - o dado seguinte (3x0201) passou a ter um texto novo
//   TRIGGER_CACHE  - a quantidade de dados em cache (3x0002) aumentou
// Os dois ligados = qualquer um dispara. Ajustar depois do teste na fabrica.
#define TRIGGER_TEXTO   1
#define TRIGGER_CACHE   1

// Como apagar o codigo da impressora depois do rele:
//   0 = Modbus, bobina 0x0002 ("limpar cache de dados de rede")
//   1 = protocolo privado, porta 4008, comando 0x10
#define LIMPAR_METODO   0
#define LIMPAR_TENTATIVAS 3

// ---- Esteira (referencia do LED 24V do botao pulsante) -------------------
// 0 = IN1 do StamPLC (recebe o 24V do LED do botao: aceso = esteira rodando)
// -1 = (modo bancada, sem IN1) considera esteira ligada quando o rele esta fechado
#define ESTEIRA_ENTRADA     0
#define ESTEIRA_ATIVO_ALTO  1   // 1 = entrada com tensao (24V) significa esteira ligada
