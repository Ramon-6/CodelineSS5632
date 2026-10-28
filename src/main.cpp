// Leitura de codigo de barras na impressora -> fecha rele -> apaga o codigo.
// StamPLC (M5StampS3) + modulo PoE (W5500). Fala Modbus TCP com a impressora.
#include <M5StamPLC.h>
#include <SPI.h>
#include <M5_Ethernet.h>
#include <Preferences.h>
#include "config.h"
#include "relatorio.h"

// ---- Pinos do modulo PoE (W5500)/ ------------------------------------
static const int PIN_SCK = 7, PIN_MISO = 9, PIN_MOSI = 8, PIN_CS = 11;

static uint8_t mac[6] = {0x02, 0x5A, 0x50, 0x4C, 0x43, 0x01};
static IPAddress ipLocal(STAMPLC_IP), ipGw(STAMPLC_GATEWAY), ipMask(STAMPLC_MASK);
static IPAddress ipImpressora(IMPRESSORA_IP);
static uint32_t esteiraSegMs = RELE_TEMPO_MS;
static Preferences prefs;  // ajustes salvos na placa (sobrevivem a reinicio)

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
// Cliente Modbus persistente - mantem conexao aberta entre transacoes
static EthernetClient mb;
static bool mbTransacao(const uint8_t* pdu, size_t n, uint8_t* resp, size_t& rlen) {
  // reconecta se cair
  if (!mb.connected()) {
    mb.stop();
    mb.setConnectionTimeout(1500);
    if (!mb.connect(ipImpressora, IMPRESSORA_PORTA)) {
      return false;
    }
  }
  while (mb.available()) mb.read();
  tid++;
  uint8_t cab[7] = {uint8_t(tid >> 8), uint8_t(tid), 0, 0, uint8_t((n + 1) >> 8), uint8_t(n + 1), MODBUS_UNIT};
  mb.write(cab, 7);
  mb.write(pdu, n);
  unsigned long t0 = millis();
  uint8_t h[7];
  size_t got = 0;
  while (got < 7) {
    if (millis() - t0 > 1500) { mb.stop(); return false; }
    if (mb.available()) h[got++] = mb.read();
  }
  size_t len = (h[4] << 8 | h[5]) - 1;
  if (len < 1 || len > 250) { mb.stop(); return false; }
  got = 0;
  while (got < len) {
    if (millis() - t0 > 2000) { mb.stop(); return false; }
    if (mb.available()) resp[got++] = mb.read();
  }
  rlen = len;
  // NAO fecha - mantem conexao aberta para proximas transacoes
  if (resp[0] & 0x80) return false;
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

// Le varios bits de uma vez (ate 256). fc=1 bobinas, fc=2 discrete inputs.
static bool lerBits(uint8_t fc, uint16_t addr, uint16_t qty, bool* out) {
  if (qty > 256) qty = 256;
  uint8_t pdu[5] = {fc, uint8_t(addr >> 8), uint8_t(addr), uint8_t(qty >> 8), uint8_t(qty)};
  uint8_t r[64];
  size_t rl;
  size_t need = (qty + 7) / 8;
  if (!mbTransacao(pdu, 5, r, rl) || rl < 2 + need) return false;
  for (uint16_t i = 0; i < qty; i++) out[i] = (r[2 + i / 8] >> (i % 8)) & 1;
  return true;
}

static bool escreverBobina(uint16_t addr, bool on) {
  uint8_t pdu[5] = {0x05, uint8_t(addr >> 8), uint8_t(addr), uint8_t(on ? 0xFF : 0), 0};
  uint8_t r[16];
  size_t rl;
  return mbTransacao(pdu, 5, r, rl);
}

// ---- Protocolo privado (porta 4008) ----------------
// Envia comando pra impressora via porta 4008 e espera resposta curta
static bool enviar4008(const uint8_t* cmd, size_t n) {
  EthernetClient c;
  c.setConnectionTimeout(1000);
  if (!c.connect(ipImpressora, 4008)) return false;
  c.write(cmd, n);
  unsigned long t0 = millis();
  while (millis() - t0 < 1200) {
    if (c.available() >= 8) { c.stop(); return true; }
  }
  c.stop();
  return false;
}

// Comando 0x10 - limpa cache de dados de rede (nao apaga codigo do leitor)
static bool limparPorta4008() {
  const uint8_t cmd[8] = {0x1B, 0x1F, 0x00, 0x00, 0x10, 0x00, 0x1C, 0x17};
  return enviar4008(cmd, 8);
}

// Comando 0x33 - FORCA reload template (apaga o codigo do leitor USB)
static bool recarregarTemplate() {
  const char* nome = TEMPLATE_NOME;
  size_t nlen = strlen(nome);
  uint8_t pkt[64];
  pkt[0] = 0x1B; pkt[1] = 0x1F;
  pkt[2] = nlen & 0xFF; pkt[3] = (nlen >> 8) & 0xFF;
  pkt[4] = 0x33;
  memcpy(pkt + 5, nome, nlen);
  pkt[5 + nlen] = 0x00;
  pkt[6 + nlen] = 0x1C;
  pkt[7 + nlen] = 0x17;
  // libera o socket do Modbus temporariamente (W5500 tem poucos sockets)
  mb.stop();
  delay(50);
  EthernetClient c;
  c.setConnectionTimeout(1500);
  Serial.print("[0x33] conectar 4008... ");
  if (!c.connect(ipImpressora, 4008)) {
    Serial.println("FALHOU conectar");
    return false;  // caller trata a falha (contador de tentativas)
  }
  Serial.print("OK, enviando... ");
  size_t sent = c.write(pkt, 8 + nlen);
  Serial.printf("%u/%u bytes\r\n", (unsigned)sent, (unsigned)(8 + nlen));
  // aguarda resposta um pouco mais e ignora conteudo
  unsigned long t0 = millis();
  int recv = 0;
  while (millis() - t0 < 1500) {
    if (c.available()) { c.read(); recv++; }
    if (recv >= 16) break;
  }
  Serial.printf("[0x33] recebeu %d bytes de resposta\r\n", recv);
  c.stop();
  return true;
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
  char sn[20] = "";  // 4x1001-1014 = Serial Number (14 chars) - onde o leitor pode gravar
  bool pronta = false, alarme = false;
  bool coverOpen = false;  // 1x0018 - tampa do cabecote aberta
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
    case 1: {  // cache + contador (menos critico - roda a cada 5 ciclos)
      static int cnt = 0;
      if (cnt++ < 5) { passo = 11; return true; }  // pula direto pro texto
      cnt = 0;
      if (!lerRegs(4, 1, 1, r)) { passo = 0; return false; }
      s.cache = r[0];
      if (!lerRegs(3, 60, 2, r)) { passo = 0; return false; }
      s.contador = uint32_t(r[0]) << 16 | r[1];
      passo = 11;
      return true;
    }
    case 11: {  // texto onde o LEITOR USB escreve: 4x0201 (HOLDING, fc=03)!
      if (!lerRegs(3, 199, 16, r)) { passo = 0; return false; }
      // le addr 199 (flag 4x0200) + addr 200..214 (texto)
      // nao usamos r[0] (flag); texto comeca em r[1]
      int n = 0;
      for (int i = 1; i < 16 && n < 48; i++) {  // comeca em r[1] = 4x0201
        char c = r[i] & 0xFF;
        if (c == 0x09 || c == 0) break;
        if (c >= 32 && c < 127) s.texto[n++] = c;
      }
      s.texto[n] = 0;
      // volta direto pro texto (gatilho mais rapido!)
      passo = 1;  // mas o case 1 pula pro 11 nas primeiras 5x = texto roda rapido
      ciclado = true;
      return true;
    }
    case 2: {  // alarme (1x0001) + tampa (1x0018) + pronta (1x0100) em uma leitura so
      bool bits[100];
      if (!lerBits(2, 0, 100, bits)) { passo = 1; return false; }
      s.alarme = bits[0];       // 1x0001
      s.coverOpen = bits[17];   // 1x0018 - cabecote aberto
      s.pronta = bits[99];      // 1x0100
      passo = 4;
      return true;
    }
    case 4: {  // estado (3x0021)
      if (!lerRegs(4, 20, 1, r)) { passo = 1; return false; }
      s.estado = r[0];
      passo = 5;
      return true;
    }
    case 5: {  // 4x0201-4x0220 HOLDING (Network data transmit register) - OUTRA opcao do leitor
      if (!lerRegs(3, 200, 20, r)) { passo = 1; return false; }
      char buf[48];
      int n = 0;
      for (int i = 0; i < 20 && n < 46; i++) {
        char c = r[i] & 0xFF;
        if (c >= 32 && c < 127) buf[n++] = c;
        else if (c == 0) break;
      }
      buf[n] = 0;
      static char ultH201[48] = "";
      if (strcmp(buf, ultH201)) {
        Serial.printf("[H4x0201] [%s] -> [%s]\r\n", ultH201, buf);
        strncpy(ultH201, buf, sizeof ultH201);
      }
      Serial.printf("[LER] 4x0201h=[%s]\r\n", buf);
      passo = 6;
      return true;
    }
    case 6: {  // SN (4x1001-1014) - onde o leitor de codigo pode gravar
      if (!lerRegs(3, 1000, 14, r)) { passo = 1; return false; }
      int n = 0;
      for (int i = 0; i < 14 && n < 19; i++) {
        char c = r[i] & 0xFF;
        if (c >= 32 && c < 127) s.sn[n++] = c;
      }
      s.sn[n] = 0;
      Serial.printf("[LER] SN=[%s]\r\n", s.sn);
      passo = 1;
      return true;
    }
  }
  passo = 0;
  return false;
}

// Pede para limpar o codigo visivel na impressora
static bool pedirLimparCodigo() {
  if (LIMPAR_METODO == 2) return recarregarTemplate();  // apaga codigo do leitor
  if (LIMPAR_METODO == 1) return limparPorta4008();     // so cache de rede
  return escreverBobina(1, true);                       // coil Modbus (nao funciona pra leitor)
}
static bool jaLimpou() {
  if (LIMPAR_METODO == 2) return true;  // reload template e sincrono
  bool v = true;
  if (!lerBit(1, 1, v)) return false;
  return !v;
}

// ---- Deteccao MIX A/B no texto do codigo (ponto 3) ---------------------
// Procura "MIX A" ou "MIX B" (case-insensitive, aceita " MIXA"/"MIXB" tambem).
static char detectaMix(const char* texto) {
  if (!texto || !*texto) return 0;
  char up[64];
  int n = 0;
  for (int i = 0; texto[i] && n < 63; i++) {
    char c = texto[i];
    up[n++] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
  }
  up[n] = 0;
  // procura MIX A, MIX_A, MIXA e idem B
  if (strstr(up, "MIX A") || strstr(up, "MIXA")) return 'A';
  if (strstr(up, "MIX B") || strstr(up, "MIXB")) return 'B';
  return 0;
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
enum Fase { OCIOSO, PRE_PARA_PULSA, PRE_PARA_ESPERA, PULSA_LIGA, AGUARDA_LIGA, RODANDO, PAUSADA,
            PULSA_PARA, AGUARDA_PARA, PARA_INSISTE, PARA_SEM_CODIGO, LIMPANDO, ERRO };
static Fase fase = OCIOSO;
static const char* erroMsg = "";
static unsigned long tLimpar = 0, tFase = 0, tPoll = 0, tTela = 0, tRes = 0;
static const uint32_t PULSO_MS = 1000;      // duracao do pulso (clique)
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
static bool usaDhcp = false;  // true = pega IP automatico do roteador
static uint32_t esperaRetomarMs = 60000;  // quanto esperar pelo operador religar a esteira (padrao 1 min)
static uint32_t tempoRestanteMs = 0;      // o que falta do RODANDO quando pausa

// ---- Receitas MIX A / MIX B (ponto 3) --------------------------------
static uint32_t tempoMixAMs = TEMPO_MIX_A_MS;
static uint32_t tempoMixBMs = TEMPO_MIX_B_MS;
static uint32_t cicloTempoMs = RELE_TEMPO_MS;  // tempo do ciclo em andamento (depende do mix)
static char cicloMix = 0;                      // 'A', 'B' ou 0

// ---- Grace period boot (ponto 1) -------------------------------------
static unsigned long tBoot = 0;

// ---- Modo manual (ponto 5) -------------------------------------------
static bool modoManual = false;
static bool in2Anterior = false;
static unsigned long tPulsoManual = 0;

// ---- Sirene limpeza (ponto 4) ----------------------------------------
static bool sireneHabilitada = SIRENE_LIMPEZA_HABILITADO;
static int sireneHoras[3] = {8, 13, 18};
static int sireneMinutos[3] = {0, 0, 0};
static bool sireneAtiva = false;
static bool sireneDisparadoHoje[3] = {false, false, false};
static int sireneUltDia = -1;
static bool tampaAnterior = false;
static unsigned long tTampaAbriu = 0;      // quando a tampa foi aberta nesta janela
static bool sireneTampaValida = false;     // tampa aberta por >= SIRENE_TAMPA_MIN_ABERTA_MS

// ---- Encoder/contador (ponto 6) --------------------------------------
static bool encoderFimHabilitado = ENCODER_FIM_HABILITADO;
static uint32_t encoderUltContador = 0;
static unsigned long tUltMudouContador = 0;
static unsigned long tLogContador = 0;

// Momento em que o ultimo ciclo terminou - usado pra mostrar FIM DE CICLO
// na tela por 3s antes de voltar ao PRONTO.
unsigned long tMostraFim = 0;

// Estado da limpeza (global - antes era static dentro do loop e ficava com estado
// errado se operador cancelasse via pararTudo durante LIMP_ESPERAR)
static enum { LIMP_PEDIR, LIMP_ESPERAR } estLimpa = LIMP_PEDIR;
static int limpTentativas = 0;  // contador para desistir depois de LIMPANDO_MAX_TENTATIVAS

// Vai pro estado ERRO limpando o buzzer, luz vermelha e tempo resetado. Em 5s
// o loop pega e manda pro LIMPANDO (que apaga codigo e volta ao OCIOSO). Nunca
// trava o operador: ele so precisa ler codigo de novo.
static void irParaErro(const char* msg) {
  erroMsg = msg;
  fase = ERRO;
  tFase = millis();
  M5StamPLC.noTone();
  M5StamPLC.setStatusLight(255, 0, 0);
  avisof("ERRO: %s", msg);
  relEvento("ERRO", ultCodigo, msg);
}

// Carrega/salva horarios da sirene em formato "HH:MM,HH:MM,HH:MM"
static void sireneSerializa(char* out, size_t n) {
  snprintf(out, n, "%02d:%02d,%02d:%02d,%02d:%02d",
           sireneHoras[0], sireneMinutos[0],
           sireneHoras[1], sireneMinutos[1],
           sireneHoras[2], sireneMinutos[2]);
}
static void sireneParseia(const char* s) {
  int h, m, k = 0;
  while (*s && k < 3) {
    while (*s == ' ' || *s == ',') s++;
    if (sscanf(s, "%d:%d", &h, &m) == 2) {
      if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
        sireneHoras[k] = h; sireneMinutos[k] = m; k++;
      }
    }
    while (*s && *s != ',') s++;
  }
}

#include "tela.h"
#include "paginas.h"
#include "manutencao.h"
#include "grove_cardputer.h"

// ---- Implementacao dos hooks que manutencao.h e grove_cardputer.h declaram ----
// Snapshot JSON do estado atual (serve ao /live.json E ao comando LIVE do Grove)
const char* nomeFaseAtual() {
  switch (fase) {
    case OCIOSO: return "OCIOSO";
    case PRE_PARA_PULSA: case PRE_PARA_ESPERA: return "PREPARANDO";
    case PULSA_LIGA: case AGUARDA_LIGA: return "LIGANDO";
    case RODANDO: return "OPERANDO";
    case PAUSADA: return "PAUSADA";
    case PULSA_PARA: case AGUARDA_PARA: case PARA_INSISTE: return "PARANDO";
    case PARA_SEM_CODIGO: return "FIM";
    case LIMPANDO: return "LIMPANDO";
    case ERRO: return "ERRO";
  }
  return "?";
}

static uint32_t tempoAlvoCiclo() {
  // detecta MIX A/B no codigo lido pelo scanner
  if (MIX_RECIPE_HABILITADO && ultCodigo[0]) {
    String s = ultCodigo; s.toUpperCase();
    if (s.indexOf("MIX A") >= 0 || s.indexOf("MIXA") >= 0) return tempoMixAMs;
    if (s.indexOf("MIX B") >= 0 || s.indexOf("MIXB") >= 0) return tempoMixBMs;
  }
  return esteiraSegMs;
}

int manutencaoColetaStatus(char* buf, size_t n) {
  uint32_t tRest = 0;
  if (fase != OCIOSO && fase != ERRO) {
    uint32_t alvo = tempoAlvoCiclo();
    uint32_t decorrido = (uint32_t)(millis() - tFase);
    tRest = decorrido < alvo ? (alvo - decorrido) / 1000 : 0;
  }
  return snprintf(buf, n,
    "{\"fase\":\"%s\",\"codigo\":\"%s\",\"hora_leitura\":\"%s\","
    "\"rele\":%d,\"esteira\":%d,\"modo\":\"%s\","
    "\"sirene_hab\":%d,\"encoder\":%lu,\"encoder_fim_hab\":%d,"
    "\"tempo_alvo_s\":%lu,\"tempo_restante_s\":%lu,"
    "\"tempo_mixa_s\":%lu,\"tempo_mixb_s\":%lu,"
    "\"ip_plc\":\"%s\",\"ip_impressora\":\"%s\","
    "\"ap_ativo\":%d,\"up_ms\":%lu}",
    nomeFaseAtual(), ultCodigo, ultHora,
    releLigado ? 1 : 0, esteiraLigada ? 1 : 0,
    modoManual ? "manual" : "auto",
    sireneHabilitada ? 1 : 0,
    (unsigned long)ultimoSnap.contador,
    encoderFimHabilitado ? 1 : 0,
    (unsigned long)(tempoAlvoCiclo() / 1000),
    (unsigned long)tRest,
    (unsigned long)(tempoMixAMs / 1000),
    (unsigned long)(tempoMixBMs / 1000),
    Ethernet.localIP().toString().c_str(),
    ipImpressora.toString().c_str(),
    manutencaoAPAtivo() ? 1 : 0,
    (unsigned long)millis()
  );
}

// Despeja log circular em um Client (HTTP) ou em qualquer Stream (Serial Grove).
template<typename T> static void despejaLog(T& out) {
  for (int i = 0; i < LOG_N; i++) {
    int idx = (logPos + i) % LOG_N;
    if (logBuf[idx][0]) { out.println(logBuf[idx]); }
  }
}
void manutencaoDespejaLog(Client& c) { despejaLog(c); }
void manutencaoDespejaLogSerial(Stream& s) { despejaLog(s); }

// Hooks do Grove: implementacoes concretas (chamam as funcoes ja existentes do main)
static void pararTudo();  // forward (ja declarado abaixo)
void grovePararCiclo() { pararTudo(); }
void groveDispararTeste() {
  if (fase == OCIOSO) disparar("grove", "TESTE-GROVE");
}
bool groveMudaTempo(uint32_t ms) {
  if (ms < 60000 || ms > 36000000UL) return false;
  esteiraSegMs = ms;
  prefs.putUInt("etempo", ms);
  return true;
}
void groveSetModo(bool manual) {
  modoManual = manual;
  prefs.putBool("manual", manual);
  relEvento("MODO", "", manual ? "manual" : "auto");
}
uint32_t groveEncoder() { return ultimoSnap.contador; }
const char* groveFaseNome() { return nomeFaseAtual(); }

// ---- Maquina de estados -------------------------------------------------
// Chamado quando um codigo foi lido pela impressora (ou botao de teste)
static void disparar(const char* motivo, const char* codigo) {
  // Modo manual ignora disparos automaticos da impressora (ponto 5)
  if (modoManual && strcmp(motivo, "botao B") != 0 && strcmp(motivo, "botao B apos erro") != 0) {
    logf("Ignorado (modo manual): %s %s", motivo, codigo);
    return;
  }
  // Determina tempo do ciclo pelo MIX detectado no texto (ponto 3)
  cicloMix = MIX_RECIPE_HABILITADO ? detectaMix(codigo) : 0;
  if (cicloMix == 'A') cicloTempoMs = tempoMixAMs;
  else if (cicloMix == 'B') cicloTempoMs = tempoMixBMs;
  else cicloTempoMs = esteiraSegMs;

  char det[32];
  if (cicloMix) snprintf(det, sizeof det, "%s MIX_%c %umin", motivo, cicloMix, (unsigned)(cicloTempoMs / 60000));
  else snprintf(det, sizeof det, "%s %umin", motivo, (unsigned)(cicloTempoMs / 60000));
  relEvento("CODIGO", codigo, det);
  disparos++;
  falhaLimpeza = false;
  snprintf(ultCodigo, sizeof ultCodigo, "%s", codigo);
  char d[16];
  carimbo(d, ultHora);
  if (cicloMix) avisof("Codigo (MIX %c): %s", cicloMix, codigo);
  else avisof("Codigo lido: %s", codigo);
  logf("DISPARO (%s) -> ciclo %u min", motivo, (unsigned)(cicloTempoMs / 60000));
  // Se a esteira ja esta rodando, para ela antes para garantir ciclo cheio
  rele(true);
  tFase = millis();
  encoderUltContador = ultimoSnap.contador;
  tUltMudouContador = millis();
  if (esteiraLigada) {
    avisof("Esteira ja rodando: parando antes");
    fase = PRE_PARA_PULSA;
  } else {
    fase = PULSA_LIGA;
  }
}

// Soma/subtrai 1 minuto do tempo da esteira rodando (botoes B/C na tela de Ajustes)
static void mudaTempo(int delta) {
  long novo = (long)esteiraSegMs + delta;
  if (novo < 60000L || novo > 3600000L * 10) return;  // 1 min a 10 h
  esteiraSegMs = (uint32_t)novo;
  prefs.putUInt("etempo", esteiraSegMs);
  tSalvo = millis();
  logf("Tempo da esteira: %u min", (unsigned)esteiraSegMs / 60000);
}

// Abre o rele e cancela qualquer ciclo em andamento (botao C e botao da web).
// Se havia um codigo lido e a impressora esta online, tambem apaga o codigo.
// Em bancada (sem impressora), apenas limpa o estado para nao ficar insistindo em apagar.
static void pararTudo() {
  rele(false);
  M5StamPLC.noTone();
  bool haviaCodigo = fase != OCIOSO;
  falhaLimpeza = false;
  erroMsg = "";
  M5StamPLC.setStatusLight(0, 0, 0);
  temBase = false;
  cicloMix = 0;
  estLimpa = LIMP_PEDIR;
  limpTentativas = 0;
  tFase = millis();
  if (haviaCodigo) {
    avisof("Parado - apagando codigo");
    relEvento("CANCELADO");
    fase = LIMPANDO;  // mesmo offline: LIMPANDO tenta e desiste depois
    tLimpar = 0;
  } else {
    fase = OCIOSO;
  }
}

void setup() {
  M5StamPLC.begin();
  // inicializa canal LEDC do buzzer (senao tone() da erro na primeira chamada)
  M5StamPLC.tone(2000, 50);
  delay(60);
  M5StamPLC.noTone();
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
  esteiraSegMs = prefs.getUInt("etempo", RELE_TEMPO_MS);  // padrao: 8 min
  if (esteiraSegMs < 60000) esteiraSegMs = RELE_TEMPO_MS;  // migra valores antigos
  esteiraEntrada = prefs.getInt("eent", ESTEIRA_ENTRADA);
  esteiraAlto = prefs.getBool("ealto", ESTEIRA_ATIVO_ALTO);
  usaDhcp = prefs.getBool("dhcp", false);
  esperaRetomarMs = prefs.getUInt("esper", 60000);  // 1 min por padrao
  tempoMixAMs = prefs.getUInt("mixa", TEMPO_MIX_A_MS);
  tempoMixBMs = prefs.getUInt("mixb", TEMPO_MIX_B_MS);
  modoManual = prefs.getBool("manual", false);
  sireneHabilitada = prefs.getBool("sirH", SIRENE_LIMPEZA_HABILITADO);
  encoderFimHabilitado = prefs.getBool("encF", ENCODER_FIM_HABILITADO);
  String sirStr = prefs.getString("sirHor", SIRENE_HORARIOS_PADRAO);
  sireneParseia(sirStr.c_str());
  tBoot = millis();
  // Baseline das entradas para nao ter borda de subida fantasma no boot
  in2Anterior = M5StamPLC.readPlcInput(MODO_MANUAL_BOTAO_IN);
  esteiraLigada = esteiraCand = (esteiraEntrada >= 0)
    ? (M5StamPLC.readPlcInput(esteiraEntrada) == esteiraAlto) : false;
  // Mensagem no display durante o boot: senao fica preto esperando DHCP
  {
    auto& d = M5StamPLC.Display();
    d.fillScreen(0xFFFF);
    d.setFont(&fonts::Font4);
    d.setTextColor(0x0000, 0xFFFF);
    d.setCursor(10, 40);
    d.print("Iniciando...");
    d.setFont(&fonts::Font2);
    d.setCursor(10, 80);
    d.printf("Rede: %s", usaDhcp ? "aguardando DHCP (5s)" : "IP fixo");
  }
  if (usaDhcp) {
    logf("Rede: pedindo IP via DHCP (timeout 5s)...");
    // timeout curto (5s) e response rapido (1s) pra nao travar o boot em cabo direto
    if (!Ethernet.begin(mac, 5000, 1000)) {
      logf("DHCP falhou - voltando para IP fixo");
      usaDhcp = false;
      prefs.putBool("dhcp", false);
      Ethernet.begin(mac, ipLocal, ipGw, ipGw, ipMask);
    }
  } else {
    Ethernet.begin(mac, ipLocal, ipGw, ipGw, ipMask);
  }
  web.begin();
  manutencaoBegin();
  groveBegin();
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

// ---- Modo manual: IN2 pulsa o rele (ponto 5) --------------------------
static void processaModoManual() {
  if (!MODO_MANUAL_HABILITADO || !modoManual) return;
  bool btn = M5StamPLC.readPlcInput(MODO_MANUAL_BOTAO_IN);
  if (btn && !in2Anterior) {  // borda de subida
    logf("Manual: botao pressionado - pulsando rele");
    relEvento("MANUAL_PULSO");
    rele(true);
    tPulsoManual = millis();
  }
  if (releLigado && tPulsoManual && millis() - tPulsoManual >= PULSO_MS && fase == OCIOSO) {
    rele(false);
    tPulsoManual = 0;
  }
  in2Anterior = btn;
}

// ---- Sirene de limpeza (ponto 4) --------------------------------------
// Em cada horario, liga R2 ate o operador abrir+fechar a tampa do cabecote.
// Nao interrompe ciclo em andamento: so dispara quando fase == OCIOSO.
// A tampa so vale como "limpou" se ficou aberta por SIRENE_TAMPA_MIN_ABERTA_MS
// (protege contra operador que so encosta no fim de curso).
static void processaSirene() {
  if (!sireneHabilitada || !horaOk) return;
  time_t n = time(nullptr);
  struct tm t;
  localtime_r(&n, &t);
  if (sireneUltDia != t.tm_mday) {
    sireneUltDia = t.tm_mday;
    for (int i = 0; i < 3; i++) sireneDisparadoHoje[i] = false;
    logf("Novo dia - sirene resetada");
  }
  int minHoje = t.tm_hour * 60 + t.tm_min;
  // So dispara se ja estiver OCIOSO - respeita ciclo em andamento. Se chegou
  // o horario mas ciclo esta rodando, fica aguardando o ciclo terminar e ai
  // aciona (sireneDisparadoHoje continua false, loop pega na proxima iteracao).
  if (fase == OCIOSO && !sireneAtiva) {
    for (int i = 0; i < 3; i++) {
      int minHorario = sireneHoras[i] * 60 + sireneMinutos[i];
      // janela de 60 min: se perdeu a janela, nao dispara mais hoje
      if (!sireneDisparadoHoje[i] && minHoje >= minHorario && minHoje < minHorario + 60) {
        sireneDisparadoHoje[i] = true;
        sireneAtiva = true;
        sireneTampaValida = false;
        tTampaAbriu = 0;
        avisof("SIRENE - hora de limpar cabecote (%02d:%02d)", sireneHoras[i], sireneMinutos[i]);
        relEvento("SIRENE_LIGADA", "", "hora limpeza");
        break;
      }
    }
  }
  // Detecta tampa aberta/fechada com debounce de SIRENE_TAMPA_MIN_ABERTA_MS
  bool tampa = ultimoSnap.coverOpen;
  if (tampa && !tampaAnterior) {
    tTampaAbriu = millis();
    sireneTampaValida = false;
    logf("Tampa do cabecote ABERTA");
  } else if (tampa && !sireneTampaValida && tTampaAbriu &&
             millis() - tTampaAbriu >= SIRENE_TAMPA_MIN_ABERTA_MS) {
    sireneTampaValida = true;
    logf("Tampa aberta ha %us - abertura valida", (unsigned)(SIRENE_TAMPA_MIN_ABERTA_MS / 1000));
  } else if (!tampa && tampaAnterior) {
    logf("Tampa do cabecote FECHADA");
    if (sireneAtiva && sireneTampaValida) {
      sireneAtiva = false;
      sireneTampaValida = false;
      tTampaAbriu = 0;
      avisof("Cabecote limpo - sirene desligada");
      relEvento("SIRENE_DESLIGADA", "", "tampa fechada");
    } else if (sireneAtiva) {
      avisof("Tampa encostada rapido - sirene continua");
      relEvento("SIRENE_IGNORADA", "", "tampa < 5s");
    }
  }
  tampaAnterior = tampa;
  static bool sireneReleAnterior = false;
  if (sireneAtiva != sireneReleAnterior) {
    M5StamPLC.writePlcRelay(SIRENE_CANAL, sireneAtiva);
    sireneReleAnterior = sireneAtiva;
  }
}

// ---- Log do contador 4x0061 (ponto 6 - fase observacao) ---------------
static void processaLogContador() {
  if (!online) return;
  if (millis() - tLogContador < (uint32_t)ENCODER_LOG_INTERVAL_S * 1000) return;
  tLogContador = millis();
  bool rodando = fase == RODANDO || fase == PAUSADA;
  char det[48];
  snprintf(det, sizeof det, "cont=%lu %s", (unsigned long)ultimoSnap.contador, rodando ? "ciclo" : "ocioso");
  relEvento("CONTADOR", "", det);
}

void loop() {
  M5StamPLC.update();
  atendeWeb();         // atende Ethernet (impressora) + WiFi AP (manutencao)
  manutencaoLoop();    // timeout do AP
  groveLoop();         // Serial1 do Grove (Cardputer)
  processaModoManual();
  processaSirene();
  processaLogContador();

  // Atalho global: botao A segurado 3s em qualquer pagina = liga/desliga AP WiFi
  // (consome o botao - impede que o wasPressed seguinte troque de pagina)
  static bool apBtnConsumido = false;
  if (M5StamPLC.BtnA().pressedFor(3000) && !apBtnConsumido) {
    apBtnConsumido = true;
    manutencaoToggleAP();
    bool on = manutencaoAPAtivo();
    avisof("AP %s", on ? "LIGADO" : "desligado");
    if (on) {
      avisof("SSID: " MANUTENCAO_AP_SSID);
      avisof("http://192.168.4.1");
    }
  }
  if (!M5StamPLC.BtnA().isPressed()) apBtnConsumido = false;

  // A = proxima tela | B = testar (ou +tempo) | C = parar (ou -tempo). Rodape da tela mostra o que vale.
  static unsigned long tBotao = 0;
  bool botao = false;
  if (M5StamPLC.BtnA().wasPressed() && !apBtnConsumido) { pagina = (pagina + 1) % N_PAG; botao = true; }
  // Atalhos de botao segurado por 1 s:
  //  Pagina 2 (ESTADOS):  B = alterna origem esteira (rele <-> IN1)
  //                       C = alterna rede (DHCP <-> IP fixo) e reinicia
  if (pagina == 2 && M5StamPLC.BtnB().pressedFor(1000)) {
    static unsigned long tAltera = 0;
    if (millis() - tAltera > 1500) {
      tAltera = millis();
      esteiraEntrada = esteiraEntrada < 0 ? 0 : -1;
      prefs.putInt("eent", esteiraEntrada);
      tSalvo = millis();
      avisof("Origem esteira: %s", esteiraEntrada < 0 ? "rele" : "IN1");
    }
  }
  if (pagina == 2 && M5StamPLC.BtnC().pressedFor(1000)) {
    static unsigned long tRede = 0;
    if (millis() - tRede > 1500) {
      tRede = millis();
      usaDhcp = !usaDhcp;
      prefs.putBool("dhcp", usaDhcp);
      avisof("Rede: %s - reiniciando", usaDhcp ? "DHCP" : "IP fixo");
      delay(600);
      ESP.restart();
    }
  }
  // Pagina AJUSTES: B segurado 2s alterna modo MANUAL/AUTO
  if (pagina == 4 && M5StamPLC.BtnB().pressedFor(2000)) {
    static unsigned long tModo = 0;
    if (millis() - tModo > 2500) {
      tModo = millis();
      modoManual = !modoManual;
      prefs.putBool("manual", modoManual);
      tSalvo = millis();
      avisof("Modo: %s", modoManual ? "MANUAL" : "AUTO");
      relEvento("MODO", "", modoManual ? "manual" : "auto");
    }
  }
  if (M5StamPLC.BtnB().wasPressed()) {
    botao = true;
    if (pagina == 4) {
      mudaTempo(60000);  // +1 minuto
    } else if (pagina == 2) {
      // na tela ESTADOS o clique curto nao faz nada (clique longo alterna origem)
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
    if (pagina == 4) mudaTempo(-60000);  // -1 minuto
    else if (pagina == 2) { /* clique curto na ESTADOS nao faz nada: longo alterna DHCP */ }
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
  // ERRO auto-recupera em ERRO_RESET_MS. SEMPRE vai pro LIMPANDO (que tem contador
  // de tentativas): apaga codigo da impressora e volta pro OCIOSO - operador vai
  // ser forcado a ler codigo novo. Operador NAO tem acesso ao PLC, entao erro
  // NUNCA pode impedir ele de trabalhar.
  if (fase == ERRO && dt >= ERRO_RESET_MS) {
    avisof("Erro resolvido automaticamente - apagando codigo");
    falhaLimpeza = false;
    M5StamPLC.setStatusLight(0, 0, 0);
    temBase = false;
    estLimpa = LIMP_PEDIR;
    limpTentativas = 0;
    fase = LIMPANDO;
    tLimpar = 0;  // dispara primeira tentativa agora
    tFase = millis();
  }
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
        irParaErro("Esteira nao parou (pre-ciclo)");
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
        avisof("Esteira ligou; rodando %u min%s", (unsigned)cicloTempoMs / 60000,
               cicloMix == 'A' ? " (MIX A)" : cicloMix == 'B' ? " (MIX B)" : "");
        fase = RODANDO;
        tFase = millis();
      } else if (dt >= AGUARDA_MS) {
        irParaErro("Esteira nao ligou (sem 24V na IN1)");
      }
      break;
    case RODANDO:  // esteira trabalhando; cronometra ate dar o tempo configurado
      if (!esteiraLigada) {  // esteira parou no meio - pausa o ciclo e aguarda operador religar
        tempoRestanteMs = (dt >= cicloTempoMs) ? 0 : (cicloTempoMs - dt);
        avisof("Esteira parou - aguardando religar");
        relEvento("PAUSADA", ultCodigo, "aguardando operador");
        M5StamPLC.setStatusLight(255, 180, 0);  // amarelo
        fase = PAUSADA;
        tFase = millis();
      } else if (dt >= cicloTempoMs) {
        rele(true);  // pulso para PARAR
        fase = PULSA_PARA;
        tFase = millis();
      } else if (encoderFimHabilitado && online && dt >= ENCODER_AQUECIMENTO_MS) {
        // Ponto 6: se contador parou de subir por tempo X, finaliza ciclo
        if (ultimoSnap.contador != encoderUltContador) {
          encoderUltContador = ultimoSnap.contador;
          tUltMudouContador = millis();
        } else if (millis() - tUltMudouContador >= ENCODER_PARADO_MS) {
          avisof("Encoder parado %us - finalizando ciclo", (unsigned)(ENCODER_PARADO_MS / 1000));
          relEvento("FIM_POR_ENCODER", ultCodigo, "contador parou");
          rele(true);
          fase = PULSA_PARA;
          tFase = millis();
        }
      }
      break;
    case PAUSADA:  // esteira caiu no meio: operador tem esperaRetomarMs para religar
      {
        static unsigned long tBeepP = 0;
        if (millis() - tBeepP >= 1000) { tBeepP = millis(); M5StamPLC.tone(1800, 150); }
      }
      if (esteiraLigada) {
        M5StamPLC.noTone();
        avisof("Esteira voltou - continuando (restam %lus)", (unsigned long)(tempoRestanteMs / 1000));
        relEvento("RETOMADA", ultCodigo, "operador religou");
        M5StamPLC.setStatusLight(0, 255, 0);
        // volta pro RODANDO ajustando tFase para contar so o tempo restante
        fase = RODANDO;
        tFase = millis() - (cicloTempoMs - tempoRestanteMs);
      } else if (dt >= esperaRetomarMs) {
        M5StamPLC.noTone();
        avisof("Tempo esgotado - cancelando ciclo");
        relEvento("CANCELADO_TIMEOUT", ultCodigo, "esteira nao religou");
        M5StamPLC.setStatusLight(0, 0, 0);
        fase = LIMPANDO;
      }
      break;
    case PULSA_PARA:
      if (dt >= PULSO_MS) {
        rele(false);
        fase = AGUARDA_PARA;
        tFase = millis();
      }
      break;
    case AGUARDA_PARA:  // tolera ate TOLERANCIA_PARAR_MS (LED pode demorar pra cair)
      if (!esteiraLigada) {
        M5StamPLC.noTone();
        avisof("Esteira parou; apagando codigo");
        fase = LIMPANDO;
      } else if (dt >= TOLERANCIA_PARAR_MS) {
        avisof("Aguardando esteira cair - toca buzzer");
        relEvento("AGUARDANDO_PARAR", ultCodigo, "LED ainda alto");
        M5StamPLC.setStatusLight(255, 180, 0);
        fase = PARA_INSISTE;
        tFase = millis();
      }
      break;
    case PARA_INSISTE:  // nao parou na janela curta: toca buzzer, insiste ate tolerancia
      {
        static unsigned long tBeepI = 0;
        if (millis() - tBeepI >= 1000) { tBeepI = millis(); M5StamPLC.tone(2000, 150); }
      }
      if (!esteiraLigada) {
        M5StamPLC.noTone();
        avisof("Esteira finalmente parou; apagando codigo");
        fase = LIMPANDO;
      } else if (dt >= esperaRetomarMs) {
        // nao parou no tempo tolerado: apaga o codigo mas continua esperando esteira parar
        avisof("Tempo esgotado - apagando codigo, esteira continua");
        relEvento("PARA_FORCADO", ultCodigo, "apagou mas esteira ainda roda");
        pedirLimparCodigo();  // tenta apagar na impressora
        fase = PARA_SEM_CODIGO;
        tFase = millis();
      }
      break;
    case PARA_SEM_CODIGO:  // codigo ja apagado, aguarda esteira cair pra voltar ao ocioso
      {
        static unsigned long tBeepS = 0;
        if (millis() - tBeepS >= 1500) { tBeepS = millis(); M5StamPLC.tone(1500, 150); }
      }
      if (!esteiraLigada) {
        M5StamPLC.noTone();
        M5StamPLC.setStatusLight(0, 0, 0);
        avisof("Esteira parou - pronto");
        relEvento("PRONTO_APOS_FORCADO");
        temBase = false;
        cicloMix = 0;
        fase = OCIOSO;
      } else if (dt >= TEMPO_MAX_FASE_MS) {
        // teto global: nao fica preso esperando esteira cair pra sempre
        irParaErro("Esteira travou - LED nunca caiu");
      }
      break;
    default:
      break;
  }

  // Limpeza do codigo: faz 1 passo a cada 500ms, com teto de tentativas pra
  // nao travar o PLC se a impressora estiver offline. Esgotou -> forca OCIOSO
  // (operador vai ler codigo novo e esse ciclo volta a tentar apagar na proxima leitura).
  if (fase == LIMPANDO && millis() - tLimpar >= 500) {
    tLimpar = millis();
    if (estLimpa == LIMP_PEDIR) {
      limpTentativas++;
      if (pedirLimparCodigo()) {
        estLimpa = LIMP_ESPERAR;
      } else {
        if (!falhaLimpeza) { avisof("Nao apagou; insistindo (%d/%d)", limpTentativas, LIMPANDO_MAX_TENTATIVAS); relEvento("FALHA_APAGAR"); }
        falhaLimpeza = true;
        M5StamPLC.setStatusLight(255, 0, 0);
        if (limpTentativas >= LIMPANDO_MAX_TENTATIVAS) {
          avisof("Desisti de apagar - forcando OCIOSO");
          relEvento("NAO_LIMPOU", ultCodigo, "impressora nao respondeu");
          falhaLimpeza = false;
          M5StamPLC.setStatusLight(0, 0, 0);
          temBase = false;
          fase = OCIOSO;
          estLimpa = LIMP_PEDIR;
          limpTentativas = 0;
          tMostraFim = millis();
          cicloMix = 0;
        }
      }
    } else if (jaLimpou()) {
      avisof("Codigo apagado");
      relEvento("APAGADO");
      falhaLimpeza = false;
      M5StamPLC.setStatusLight(0, 0, 0);
      temBase = false;
      fase = OCIOSO;
      estLimpa = LIMP_PEDIR;
      limpTentativas = 0;
      tMostraFim = millis();  // mostra "FIM DE CICLO" por 3s na tela
      cicloMix = 0;
    }
  }

  // Modbus em etapas: faz UMA chamada por iteracao do loop. Rapido quando online,
  // lento (6 s entre tentativas) quando offline.
  static int snapPasso = 0;
  static Snap tmp;
  if (millis() - tPoll >= (online ? 150UL : 5000UL)) {
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
        // Habilita cache de dados de rede (coil 0x0003 = addr 2) e SN enable (0x0019 = addr 24)
        escreverBobina(2, true);
        escreverBobina(24, true);
        Serial.println("[CFG] habilitou cache mode e SN enable");
        // Ponto 1: apaga codigo residual na primeira conexao (evita disparar ciclo
        // no boot quando ja tinha codigo na impressora). Mas SO se estiver ocioso -
        // nao apagar codigo valido durante um ciclo em andamento (caso impressora
        // caia e volte no meio).
        if (APAGA_AO_CONECTAR && fase == OCIOSO) {
          logf("Apagando codigo residual (boot)...");
          recarregarTemplate();
          temBase = false;  // forca re-baseline apos limpar
        }
      }
      ultimoSnap = tmp;
#if DEBUG_SCANNER_MODBUS
      // SCANNER UNIVERSAL (debug): varre varias faixas de registros procurando MUDANCAS.
      // So ativar em bancada - cada passo adiciona uma leitura Modbus pesada ao loop.
      {
        static uint16_t snapIn[500] = {0}, snapHold[500] = {0};
        static int sweepBase = 0;
        static int sweepTipo = 0;  // 0=input, 1=holding
        static bool primeira = true;
        uint16_t buf[100];
        int base = sweepBase * 100;
        if (base < 500) {
          if (sweepTipo == 0 && lerRegs(4, base, 100, buf)) {
            for (int i = 0; i < 100; i++) {
              int addr = base + i;
              bool skip = (addr >= 5 && addr <= 7) || (addr >= 13 && addr <= 18);
              if (!primeira && !skip && buf[i] != snapIn[addr]) {
                char c = buf[i] & 0xFF;
                Serial.printf("[I] 3x%04d: %u -> %u (0x%04X %c)\r\n",
                              addr + 1, snapIn[addr], buf[i], buf[i], (c >= 32 && c < 127) ? c : '.');
              }
              snapIn[addr] = buf[i];
            }
          } else if (sweepTipo == 1 && lerRegs(3, base, 100, buf)) {
            for (int i = 0; i < 100; i++) {
              int addr = base + i;
              bool skip = (addr == 58 || addr == 59);
              if (!primeira && !skip && buf[i] != snapHold[addr]) {
                char c = buf[i] & 0xFF;
                Serial.printf("[H] 4x%04d: %u -> %u (0x%04X %c)\r\n",
                              addr + 1, snapHold[addr], buf[i], buf[i], (c >= 32 && c < 127) ? c : '.');
              }
              snapHold[addr] = buf[i];
            }
          }
        }
        sweepTipo++;
        if (sweepTipo >= 2) { sweepTipo = 0; sweepBase++; if (sweepBase >= 5) { sweepBase = 0; primeira = false; } }
      }
#endif
      if (!temBase) { base = tmp; temBase = true; }
      else {
        if (strcmp(tmp.texto, base.texto)) logf("texto: [%s] -> [%s]", base.texto, tmp.texto);
        if (tmp.alarme != base.alarme) { logf("ALARME: %d -> %d", base.alarme, tmp.alarme); relEvento("ALARME", "", tmp.alarme ? "ativo" : "normal"); }
        if (fase == OCIOSO && (millis() - tBoot >= BOOT_GRACE_MS)) {
          bool porTexto = TRIGGER_TEXTO && tmp.texto[0] && strcmp(tmp.texto, base.texto) != 0;
          bool porCache = TRIGGER_CACHE && tmp.cache > base.cache;
          bool porContador = tmp.contador > base.contador;
          bool porSN = tmp.sn[0] && strcmp(tmp.sn, base.sn) != 0;  // novo: SN mudou
          if (porTexto || porCache || porContador || porSN) {
            char cod[32];
            if (porSN) snprintf(cod, sizeof cod, "%s", tmp.sn);
            else if (porTexto) snprintf(cod, sizeof cod, "%s", tmp.texto);
            else snprintf(cod, sizeof cod, "N%lu", (unsigned long)tmp.contador);
            const char* motivo = porSN ? "SN novo" : (porContador ? "contador subiu" : (porTexto ? "texto novo" : "cache subiu"));
            disparar(motivo, cod);
          }
        }
        base = tmp;
      }
    }
  }

  // Servidor TCP na porta 4040: aceita conexao da impressora e recebe os codigos

  // Durante o ciclo, atualiza mais rapido (contagem regressiva fica fluida)
  bool emCiclo = fase == RODANDO || fase == PAUSADA || fase == PULSA_LIGA || fase == AGUARDA_LIGA || fase == PULSA_PARA || fase == AGUARDA_PARA || fase == PRE_PARA_PULSA || fase == PRE_PARA_ESPERA || fase == PARA_INSISTE || fase == PARA_SEM_CODIGO;
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
