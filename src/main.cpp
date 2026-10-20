// Leitura de codigo de barras na impressora -> fecha rele -> apaga o codigo.
// StamPLC (M5StampS3) + modulo PoE (W5500). Fala Modbus TCP com a impressora.
#include <M5StamPLC.h>
#include <SPI.h>
#include <M5_Ethernet.h>
#include <Preferences.h>
#include "config.h"
#include "relatorio.h"

// ---- Pinos do modulo PoE (W5500) ------------------------------------
static const int PIN_SCK = 7, PIN_MISO = 9, PIN_MOSI = 8, PIN_CS = 11;

static uint8_t mac[6] = {0x02, 0x5A, 0x50, 0x4C, 0x43, 0x01};
static IPAddress ipLocal(STAMPLC_IP), ipGw(STAMPLC_GATEWAY), ipMask(STAMPLC_MASK);
static IPAddress ipImpressora(IMPRESSORA_IP);
static uint32_t esteiraSegMs = RELE_TEMPO_MS;
static Preferences prefs;  // ajustes salvos na placa (sobrevivem a reinicio)

static EthernetClient mb;
static EthernetServer web(80);

// ---- Log (serial + pagina web) ---------------------------------------
#define LOG_N 40
static char logBuf[LOG_N][96];
static int logPos = 0;

// Mensagens mostradas na aba Eventos da tela (so as que importam ao operador)
#define TELA_N 10
static char telaBuf[TELA_N][48];
static int telaPos = 0, telaCount = 0;

static void emite(bool paraTela, const char* msg) {
  char dia[16], hora[10];
  carimbo(dia, hora);
  snprintf(logBuf[logPos], sizeof logBuf[0], "%s %s", hora, msg);
  Serial.println(logBuf[logPos]);
  logPos = (logPos + 1) % LOG_N;
  if (paraTela) {
    snprintf(telaBuf[telaPos], sizeof telaBuf[0], "%s %s", hora, msg);
    telaPos = (telaPos + 1) % TELA_N;
    if (telaCount < TELA_N) telaCount++;
  }
}

// logf = registro tecnico (serial + aba Eventos da web); avisof = tambem aparece na tela
static void logf(const char* fmt, ...) {
  char msg[88];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  emite(false, msg);
}

static void avisof(const char* fmt, ...) {
  char msg[88];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  emite(true, msg);
}

// ---- Modbus TCP minimo ------------------------------------------------
static uint16_t tid = 0;

// Envia o PDU (fc + dados) e devolve a resposta (fc + dados). false se falhar.
// Resposta Modbus TCP tem no maximo 150 ms de espera; se cair, nao reconecta na mesma iteracao
static bool mbTransacao(const uint8_t* pdu, size_t n, uint8_t* resp, size_t& rlen) {
  if (!mb.connected()) {
    mb.stop();
    mb.setConnectionTimeout(150);
    if (!mb.connect(ipImpressora, IMPRESSORA_PORTA)) return false;
  }
  while (mb.available()) mb.read();  // descarta lixo antigo
  tid++;
  uint8_t cab[7] = {uint8_t(tid >> 8), uint8_t(tid), 0, 0, uint8_t((n + 1) >> 8), uint8_t(n + 1), MODBUS_UNIT};
  mb.write(cab, 7);
  mb.write(pdu, n);

  unsigned long t0 = millis();
  uint8_t h[7];
  size_t got = 0;
  while (got < 7) {
    if (millis() - t0 > 250) { mb.stop(); return false; }
    if (mb.available()) h[got++] = mb.read();
  }
  size_t len = (h[4] << 8 | h[5]) - 1;
  if (len < 1 || len > 250) { mb.stop(); return false; }
  got = 0;
  while (got < len) {
    if (millis() - t0 > 300) { mb.stop(); return false; }
    if (mb.available()) resp[got++] = mb.read();
  }
  rlen = len;
  if (resp[0] & 0x80) return false;  // excecao Modbus
  return true;
}

static bool lerRegs(uint8_t fc, uint16_t addr, uint16_t qty, uint16_t* out) {  // fc 3 ou 4
  uint8_t pdu[5] = {fc, uint8_t(addr >> 8), uint8_t(addr), uint8_t(qty >> 8), uint8_t(qty)};
  uint8_t r[256];
  size_t rl;
  if (!mbTransacao(pdu, 5, r, rl) || rl < size_t(2 + qty * 2)) return false;
  for (int i = 0; i < qty; i++) out[i] = r[2 + i * 2] << 8 | r[3 + i * 2];
  return true;
}

static bool lerBit(uint8_t fc, uint16_t addr, bool& v) {  // fc 1 (bobina) ou 2 (entrada)
  uint8_t pdu[5] = {fc, uint8_t(addr >> 8), uint8_t(addr), 0, 1};
  uint8_t r[16];
  size_t rl;
  if (!mbTransacao(pdu, 5, r, rl) || rl < 3) return false;
  v = r[2] & 1;
  return true;
}

static bool escreverBobina(uint16_t addr, bool on) {
  uint8_t pdu[5] = {0x05, uint8_t(addr >> 8), uint8_t(addr), uint8_t(on ? 0xFF : 0), 0};
  uint8_t r[16];
  size_t rl;
  return mbTransacao(pdu, 5, r, rl);
}

// ---- Protocolo privado (porta 4008) so para limpar o cache ----------------
static bool limparPorta4008() {
  EthernetClient c;
  c.setTimeout(400);
  if (!c.connect(ipImpressora, 4008)) return false;
  const uint8_t cmd[8] = {0x1B, 0x1F, 0x00, 0x00, 0x10, 0x00, 0x1C, 0x17};
  c.write(cmd, 8);
  unsigned long t0 = millis();
  while (millis() - t0 < 800) {
    if (c.available() >= 8) { c.stop(); return true; }
  }
  c.stop();
  return false;
}

// ---- Estado da impressora ---------------------------------------------
// Enderecos do xlsx menos 1 (Modbus e base zero):
//   3x0002 -> 1    cache da impressora
//   3x0021 -> 20   estado da maquina
//   3x0201 -> 200  proximo dado de rede (ASCII, 1 caractere por registro)
//   4x0061 -> 60   contador de produto (32 bits)
//   1x0100 -> 99   pronta
//   1x0001 -> 0    alarme
struct Snap {
  uint16_t cache = 0, estado = 0;
  uint32_t contador = 0;
  char texto[50] = "";
  bool pronta = false, alarme = false;
};

// Round-robin: 1 chamada Modbus por iteracao, para nao travar o loop.
// Prioriza o gatilho (texto + cache). ciclado=true quando as duas leituras essenciais foram feitas.
static bool lerSnapEtapa(Snap& s, int& passo, bool& ciclado) {
  uint16_t r[64];
  ciclado = false;
  switch (passo) {
    case 0: {  // cache (3x0002) - 1 registro, bem rapido. GATILHO principal.
      if (!lerRegs(4, 1, 1, r)) { passo = 0; return false; }
      s.cache = r[0];
      passo = 1;
      return true;
    }
    case 1: {  // texto do proximo codigo (3x0201..0249) - 49 regs, dentro do limite (125)
      if (!lerRegs(4, 200, 49, r)) { passo = 0; return false; }
      int n = 0;
      for (int i = 0; i < 49; i++) {
        char c = r[i] & 0xFF;
        if (c == 0) break;
        s.texto[n++] = (c == 0x09) ? ';' : c;
      }
      s.texto[n] = 0;
      passo = 2;
      ciclado = true;  // ja temos o gatilho; o resto e detalhe
      return true;
    }
    case 2: {  // alarme (1x0001). Nao bloqueia o gatilho.
      bool v;
      if (!lerBit(2, 0, v)) { passo = 0; return false; }
      s.alarme = v;
      passo = 3;
      return true;
    }
    case 3: {  // pronta (1x0100)
      bool v;
      if (!lerBit(2, 99, v)) { passo = 0; return false; }
      s.pronta = v;
      passo = 4;
      return true;
    }
    case 4: {  // estado (3x0021)
      if (!lerRegs(4, 20, 1, r)) { passo = 0; return false; }
      s.estado = r[0];
      passo = 0;
      return true;
    }
  }
  passo = 0;
  return false;
}

// Pede para limpar; a confirmacao vem depois (sem bloquear o loop)
static bool pedirLimparCodigo() {
  if (LIMPAR_METODO == 1) return limparPorta4008();
  return escreverBobina(1, true);
}
static bool jaLimpou() {
  bool v = true;
  if (!lerBit(1, 1, v)) return false;  // sem resposta: considera que ainda nao
  return !v;
}

// ---- Rele --------------------------------------------------------------
static bool releLigado = false;
static bool esteiraLigada = false, esteiraCand = false;
static unsigned long tEsteira = 0, tDia = 0;
static bool falhaLimpeza = false;

static void rele(bool on) {
  M5StamPLC.writePlcRelay(RELE_CANAL, on);
  bool lido = M5StamPLC.readPlcRelay(RELE_CANAL);  // confirma no chip que o comando chegou
  releLigado = on;
  logf("RELE%d %s (chip=%s)", RELE_CANAL + 1, on ? "LIGOU" : "DESLIGOU", lido ? "1" : "0");
  if (lido != on) logf("ERRO: chip do rele nao respondeu");
  if (on) M5StamPLC.setStatusLight(0, 255, 0);        // LED frontal verde
  else if (falhaLimpeza) M5StamPLC.setStatusLight(255, 0, 0);
  else M5StamPLC.setStatusLight(0, 0, 0);
}

// ---- Estado compartilhado (tela e web leem daqui) -----------------------------
enum Fase { OCIOSO, PRE_PARA_PULSA, PRE_PARA_ESPERA, PULSA_LIGA, AGUARDA_LIGA, RODANDO, PULSA_PARA, AGUARDA_PARA, LIMPANDO, ERRO };
static Fase fase = OCIOSO;
static const char* erroMsg = "";
static unsigned long tLimpar = 0, tFase = 0, tPoll = 0, tTela = 0, tRes = 0;
static const uint32_t PULSO_MS = 300;      // duracao do pulso (clique)
static const uint32_t AGUARDA_MS = 2000;   // janela para confirmar que esteira respondeu
static Snap base;
static bool temBase = false;
static Snap ultimoSnap;
static bool online = false;
static bool linkOk = true;
static float fonteV = 0;
static bool relEst[4] = {false, false, false, false};
static char ipTxt[20] = "";  // lido FORA do desenho: tela, SD e rede dividem o mesmo SPI
static int disparos = 0;
static char ultCodigo[50] = "", ultHora[10] = "";
static Resumo resHoje;
static int esteiraEntrada = ESTEIRA_ENTRADA;  // -1 = segue o rele; 0..7 = entrada do StamPLC
static bool esteiraAlto = ESTEIRA_ATIVO_ALTO;

#include "tela.h"
#include "paginas.h"

// ---- Maquina de estados -------------------------------------------------
// Chamado quando um codigo foi lido pela impressora (ou botao de teste)
static void disparar(const char* motivo, const char* codigo) {
  relEvento("CODIGO", codigo, motivo);
  disparos++;
  falhaLimpeza = false;
  snprintf(ultCodigo, sizeof ultCodigo, "%s", codigo);
  char d[16];
  carimbo(d, ultHora);
  avisof("Codigo lido: %s", codigo);
  logf("DISPARO (%s) -> inicia ciclo", motivo);
  // Se a esteira ja esta rodando, para ela antes para garantir ciclo cheio
  rele(true);
  tFase = millis();
  if (esteiraLigada) {
    avisof("Esteira ja rodando: parando antes");
    fase = PRE_PARA_PULSA;
  } else {
    fase = PULSA_LIGA;
  }
}

// Soma/subtrai 1 segundo do tempo da esteira rodando (botoes B/C na tela de Ajustes)
static void mudaTempo(int delta) {
  long novo = (long)esteiraSegMs + delta;
  if (novo < 1000 || novo > 600000) return;  // 1 a 600 segundos
  esteiraSegMs = (uint32_t)novo;
  prefs.putUInt("etempo", esteiraSegMs);
  tSalvo = millis();
  logf("Tempo da esteira: %u s", (unsigned)esteiraSegMs / 1000);
}

// Abre o rele e cancela qualquer ciclo em andamento (botao C e botao da web).
// Se havia um codigo lido e a impressora esta online, tambem apaga o codigo.
// Em bancada (sem impressora), apenas limpa o estado para nao ficar insistindo em apagar.
static void pararTudo() {
  rele(false);
  bool haviaCodigo = fase != OCIOSO;
  falhaLimpeza = false;
  erroMsg = "";
  M5StamPLC.setStatusLight(0, 0, 0);
  temBase = false;
  if (haviaCodigo && online) {
    avisof("Parado - apagando codigo");
    relEvento("CANCELADO");
    fase = LIMPANDO;
    tLimpar = 0;  // dispara a primeira tentativa agora
  } else {
    if (haviaCodigo) { avisof("Parado manualmente"); relEvento("CANCELADO"); }
    fase = OCIOSO;
  }
}

void setup() {
  M5StamPLC.begin();
  rele(false);  // seguranca: sempre comeca aberto
  Serial.begin(115200);

  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
  pinMode(10, OUTPUT);  // CS do cartao SD fica em alto ate ser usado
  digitalWrite(10, HIGH);
  Ethernet.init(PIN_CS);
  prefs.begin("cfg", false);
  IPAddress tmp;
  if (tmp.fromString(prefs.getString("pip", ""))) ipImpressora = tmp;
  if (tmp.fromString(prefs.getString("ip", ""))) ipLocal = tmp;
  if (tmp.fromString(prefs.getString("gw", ""))) ipGw = tmp;
  esteiraSegMs = prefs.getUInt("etempo", 10000);
  esteiraEntrada = prefs.getInt("eent", ESTEIRA_ENTRADA);
  esteiraAlto = prefs.getBool("ealto", ESTEIRA_ATIVO_ALTO);
  Ethernet.begin(mac, ipLocal, ipGw, ipGw, ipMask);
  web.begin();
  mb.setTimeout(400);  // espera maxima ao conectar na impressora (padrao 1000 ms trava a web)
  sdIniciar();
  relogioCarrega();
  relVirada(false);
  relEvento("INICIO");
  logf("SD: %s | hora: %s", sdOk ? "ok" : "NAO ENCONTRADO", horaOk ? "ok" : "nao acertada");
  logf("Tensao de entrada: %.1f V", M5StamPLC.getPowerVoltage());
  logf("Iniciado. IP %s -> impressora %s:%d", Ethernet.localIP().toString().c_str(),
       ipImpressora.toString().c_str(), IMPRESSORA_PORTA);
  if (Ethernet.linkStatus() == LinkOFF) logf("Cabo de rede desconectado");
}

void loop() {
  M5StamPLC.update();
  atendeWeb();

  // A = proxima tela | B = testar (ou +tempo) | C = parar (ou -tempo). Rodape da tela mostra o que vale.
  static unsigned long tBotao = 0;
  bool botao = false;
  if (M5StamPLC.BtnA().wasPressed()) { pagina = (pagina + 1) % N_PAG; botao = true; }
  if (M5StamPLC.BtnB().wasPressed()) {
    botao = true;
    if (pagina == 4) {
      mudaTempo(1000);  // +1 segundo
    } else if (fase == OCIOSO) {
      disparar("botao B", "TESTE-BOTAO");
    } else if (fase == ERRO) {
      pararTudo();  // limpa o erro
      if (fase == OCIOSO) disparar("botao B apos erro", "TESTE-BOTAO");
    } else {
      avisof("Teste ignorado: em andamento");
    }
  }
  if (M5StamPLC.BtnC().wasPressed()) {
    botao = true;
    if (pagina == 4) mudaTempo(-1000);  // -1 segundo
    else pararTudo();
  }
  if (botao) { tBotao = millis(); tTela = 0; }
  if (pagina != 0 && millis() - tBotao > 60000) pagina = 0;  // sem mexer por 1 min: volta ao painel

  // Estado da esteira: le a entrada IN1 (ou segue o rele se nao configurado)
  bool ligAgora = (esteiraEntrada >= 0) ? (M5StamPLC.readPlcInput(esteiraEntrada) == esteiraAlto) : releLigado;
  if (ligAgora != esteiraCand) { esteiraCand = ligAgora; tEsteira = millis(); }
  if (esteiraCand != esteiraLigada && millis() - tEsteira >= 300) {
    esteiraLigada = esteiraCand;
    avisof(esteiraLigada ? "Esteira LIGADA" : "Esteira PARADA");
    relEvento(esteiraLigada ? "ESTEIRA_LIGADA" : "ESTEIRA_PARADA");
  }
  if (millis() - tDia >= 1000) { tDia = millis(); relVirada(esteiraLigada); }

  // Maquina de estados do ciclo da esteira
  unsigned long dt = millis() - tFase;
  switch (fase) {
    case PRE_PARA_PULSA:  // esteira estava ligada antes de ler o codigo: pulsa para PARAR
      if (dt >= PULSO_MS) {
        rele(false);
        fase = PRE_PARA_ESPERA;
        tFase = millis();
      }
      break;
    case PRE_PARA_ESPERA:  // espera a esteira parar antes de ligar de novo
      if (!esteiraLigada) {
        avisof("Esteira parou; vou ligar de novo");
        rele(true);  // pulsa para LIGAR
        fase = PULSA_LIGA;
        tFase = millis();
      } else if (dt >= AGUARDA_MS) {
        fase = ERRO;
        erroMsg = "Esteira nao parou (pre-ciclo)";
        avisof("ERRO: esteira nao parou");
        relEvento("ERRO", ultCodigo, "pre-ciclo nao parou");
        M5StamPLC.setStatusLight(255, 0, 0);
      }
      break;
    case PULSA_LIGA:  // rele fechado para dar o clique de LIGAR
      if (dt >= PULSO_MS) {
        rele(false);
        fase = AGUARDA_LIGA;
        tFase = millis();
      }
      break;
    case AGUARDA_LIGA:  // espera o LED de 24V indicar que esteira ligou
      if (esteiraLigada) {
        avisof("Esteira ligou; rodando %u s", (unsigned)esteiraSegMs / 1000);
        fase = RODANDO;
        tFase = millis();
      } else if (dt >= AGUARDA_MS) {
        fase = ERRO;
        erroMsg = "Esteira nao ligou (sem 24V na IN1)";
        avisof("ERRO: esteira nao ligou");
        relEvento("ERRO", ultCodigo, "nao ligou");
        M5StamPLC.setStatusLight(255, 0, 0);
      }
      break;
    case RODANDO:  // esteira trabalhando; cronometra ate dar o tempo configurado
      if (!esteiraLigada) {  // alguem parou manualmente a esteira
        fase = ERRO;
        erroMsg = "Esteira parou sozinha";
        avisof("ERRO: esteira parou antes do tempo");
        relEvento("ERRO", ultCodigo, "parou sozinha");
        M5StamPLC.setStatusLight(255, 0, 0);
      } else if (dt >= esteiraSegMs) {
        rele(true);  // pulso para PARAR
        fase = PULSA_PARA;
        tFase = millis();
      }
      break;
    case PULSA_PARA:
      if (dt >= PULSO_MS) {
        rele(false);
        fase = AGUARDA_PARA;
        tFase = millis();
      }
      break;
    case AGUARDA_PARA:  // confere que a esteira parou
      if (!esteiraLigada) {
        avisof("Esteira parou; apagando codigo");
        fase = LIMPANDO;
      } else if (dt >= AGUARDA_MS) {
        fase = ERRO;
        erroMsg = "Esteira nao parou";
        avisof("ERRO: esteira nao parou");
        relEvento("ERRO", ultCodigo, "nao parou");
        M5StamPLC.setStatusLight(255, 0, 0);
      }
      break;
    default:
      break;
  }

  // Limpeza do codigo: faz 1 passo por segundo e nao bloqueia o loop
  static enum { LIMP_PEDIR, LIMP_ESPERAR } estLimpa = LIMP_PEDIR;
  if (fase == LIMPANDO && millis() - tLimpar >= 500) {
    tLimpar = millis();
    if (estLimpa == LIMP_PEDIR) {
      if (pedirLimparCodigo()) estLimpa = LIMP_ESPERAR;
      else {
        if (!falhaLimpeza) { avisof("NAO apagou; vou insistir"); relEvento("FALHA_APAGAR"); }
        falhaLimpeza = true;
        M5StamPLC.setStatusLight(255, 0, 0);
      }
    } else if (jaLimpou()) {
      avisof("Codigo apagado");
      relEvento("APAGADO");
      falhaLimpeza = false;
      M5StamPLC.setStatusLight(0, 0, 0);
      temBase = false;
      fase = OCIOSO;
      estLimpa = LIMP_PEDIR;
    }
  }

  // Modbus em etapas: faz UMA chamada por iteracao do loop. Rapido quando online,
  // lento (6 s entre tentativas) quando offline.
  static int snapPasso = 0;
  static Snap tmp;
  if (millis() - tPoll >= (online ? 60UL : 6000UL)) {
    tPoll = millis();
    bool ciclado = false;
    bool etapaOk = lerSnapEtapa(tmp, snapPasso, ciclado);
    if (!etapaOk && online) {
      online = false;
      avisof("Impressora SEM resposta");
      relEvento("IMPRESSORA_OFFLINE");
    }
    if (ciclado) {
      if (!online) {
        online = true;
        avisof("Impressora conectada");
        relEvento("IMPRESSORA_ONLINE");
      }
      ultimoSnap = tmp;
      if (!temBase) { base = tmp; temBase = true; }
      else {
        if (strcmp(tmp.texto, base.texto)) logf("texto: [%s] -> [%s]", base.texto, tmp.texto);
        if (tmp.alarme != base.alarme) { logf("ALARME: %d -> %d", base.alarme, tmp.alarme); relEvento("ALARME", "", tmp.alarme ? "ativo" : "normal"); }
        if (fase == OCIOSO) {
          bool porTexto = TRIGGER_TEXTO && tmp.texto[0] && strcmp(tmp.texto, base.texto) != 0;
          bool porCache = TRIGGER_CACHE && tmp.cache > base.cache;
          if (porTexto || porCache) disparar(porTexto ? "texto novo" : "cache subiu", tmp.texto[0] ? tmp.texto : "(sem texto)");
        }
        base = tmp;
      }
    }
  }

  // Durante o ciclo, atualiza mais rapido (contagem regressiva fica fluida)
  bool emCiclo = fase == RODANDO || fase == PULSA_LIGA || fase == AGUARDA_LIGA || fase == PULSA_PARA || fase == AGUARDA_PARA || fase == PRE_PARA_PULSA || fase == PRE_PARA_ESPERA;
  if (millis() - tTela >= (emCiclo ? 100UL : 300UL)) {
    tTela = millis();
    snprintf(ipTxt, sizeof ipTxt, "%s", Ethernet.localIP().toString().c_str());
    linkOk = Ethernet.linkStatus() != LinkOFF;
    fonteV = M5StamPLC.getPowerVoltage();
    for (int i = 0; i < 4; i++) relEst[i] = M5StamPLC.readPlcRelay(i);
    desenhaTela();
  }

  if (millis() - tRes >= 5000) {  // resumo de hoje, vindo do cartao SD
    tRes = millis();
    if (sdOk) {
      char d[16], h[10];
      carimbo(d, h);
      resHoje = resumoDoDia(String(d), true);
    }
  }
}
