#pragma once
// Relatorio diario no cartao SD + paginas HTML. Incluir depois de M5StamPLC.h e M5_Ethernet.h.
#include <SD.h>
#include <time.h>
#include <sys/time.h>

static bool sdOk = false;
static bool horaOk = false;

// ---- Relogio -----------------------------------------------------------
// A hora do relogio da placa e guardada como hora local; o fuso do sistema fica em UTC
// de proposito, entao time()/localtime() devolvem os mesmos numeros do relogio.
static bool relogioCarrega() {
  struct tm t = {};
  M5StamPLC.getRtcTime(&t);
  if (t.tm_year + 1900 < 2024 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31) return horaOk = false;
  struct timeval tv = {mktime(&t), 0};
  settimeofday(&tv, nullptr);
  return horaOk = true;
}

static bool relogioAcerta(int a, int m, int d, int h, int i, int s) {
  if (a < 2024 || m < 1 || m > 12 || d < 1 || d > 31 || h > 23 || i > 59 || s > 59) return false;
  struct tm t = {};
  t.tm_year = a - 1900; t.tm_mon = m - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = i; t.tm_sec = s;
  struct timeval tv = {mktime(&t), 0};  // mktime preenche tm_wday
  settimeofday(&tv, nullptr);
  M5StamPLC.setRtcTime(&t);  // atencao: a biblioteca altera tm_year; nao reutilizar t
  horaOk = true;
  return true;
}

// dia = "AAAA-MM-DD" (ou "sem-hora"), hora = "HH:MM:SS"
static void carimbo(char* dia, char* hora) {
  if (horaOk) {
    time_t n = time(nullptr);
    struct tm t;
    localtime_r(&n, &t);
    snprintf(dia, 16, "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    snprintf(hora, 10, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
  } else {
    unsigned long s = millis() / 1000;
    strcpy(dia, "sem-hora");
    snprintf(hora, 10, "%02lu:%02lu:%02lu", (s / 3600) % 24, (s / 60) % 60, s % 60);
  }
}

// ---- Gravacao -------------------------------------------------------------
static void sdIniciar() {
  sdOk = SD.begin(10, SPI, 4000000);  // CS do cartao = pino 10 (barramento compartilhado com a rede)
  if (sdOk && !SD.exists("/rel")) SD.mkdir("/rel");
}

static void limpaCampo(char* dst, const char* src, size_t n) {
  size_t k = 0;
  for (; src && *src && k < n - 1; src++) dst[k++] = (*src == ';' || *src == '\n' || *src == '\r') ? ',' : *src;
  dst[k] = 0;
}

// Uma linha por evento: hora;evento;codigo;detalhe
static void relEvento(const char* ev, const char* codigo = "", const char* detalhe = "") {
  if (!sdOk) return;
  char dia[16], hora[10], cod[60], det[60];
  carimbo(dia, hora);
  limpaCampo(cod, codigo, sizeof cod);
  limpaCampo(det, detalhe, sizeof det);
  String path = String("/rel/") + dia + ".csv";
  bool novo = !SD.exists(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) return;
  if (novo) f.println("hora;evento;codigo;detalhe");
  f.printf("%s;%s;%s;%s\n", hora, ev, cod, det);
  f.close();
}

// Escreve o fechamento no arquivo do dia que acabou e a abertura no novo.
static char diaAtual[16] = "";
static void relVirada(bool esteiraLigada) {
  char dia[16], hora[10];
  carimbo(dia, hora);
  if (!diaAtual[0]) { strcpy(diaAtual, dia); return; }
  if (!strcmp(dia, diaAtual)) return;
  bool semHora = !strcmp(diaAtual, "sem-hora") || !strcmp(dia, "sem-hora");
  if (sdOk && !semHora) {  // fecha o dia antigo as 23:59:59 e abre o novo as 00:00:00
    String antigo = String("/rel/") + diaAtual + ".csv";
    File f = SD.open(antigo, FILE_APPEND);
    if (f) { f.println("23:59:59;DIA_FIM;;"); f.close(); }
    String novo = String("/rel/") + dia + ".csv";
    bool arquivoNovo = !SD.exists(novo);
    File g = SD.open(novo, FILE_APPEND);
    if (g) {
      if (arquivoNovo) g.println("hora;evento;codigo;detalhe");
      g.printf("00:00:00;DIA_INICIO;;%s\n", esteiraLigada ? "ligada" : "parada");
      g.close();
    }
  }
  strcpy(diaAtual, dia);
}

// ---- Leitura do relatorio ---------------------------------------------------
static int segDoDia(const String& h) {  // "HH:MM:SS"
  if (h.length() < 8) return 0;
  return h.substring(0, 2).toInt() * 3600 + h.substring(3, 5).toInt() * 60 + h.substring(6, 8).toInt();
}

static String duracao(long s) {
  char b[24];
  snprintf(b, sizeof b, "%ldh %02ldmin %02lds", s / 3600, (s / 60) % 60, s % 60);
  return b;
}

// divide "a;b;c;d" em campos
static int campos(const String& l, String* f, int n) {
  int k = 0, ini = 0;
  while (k < n) {
    int p = l.indexOf(';', ini);
    if (p < 0) { f[k++] = l.substring(ini); break; }
    f[k++] = l.substring(ini, p);
    ini = p + 1;
  }
  return k;
}

static bool lerLinha(File& f, String& l) {
  l = "";
  while (f.available()) {
    char ch = f.read();
    if (ch == '\n') return true;
    if (ch != '\r' && l.length() < 160) l += ch;
  }
  return l.length() > 0;
}

struct Resumo {
  long ligada = 0, parada = 0;
  int codigos = 0, falhas = 0, linhas = 0;
};

// Soma o tempo de esteira ligada e parada a partir dos eventos do dia.
static Resumo resumoDoDia(const String& dia, bool hoje) {
  Resumo r;
  File f = SD.open("/rel/" + dia + ".csv");
  if (!f) return r;
  int estado = -1;  // -1 desconhecido, 0 parada, 1 ligada
  long t = 0, ult = 0;
  String l, c[4];
  lerLinha(f, l);  // cabecalho
  while (lerLinha(f, l)) {
    if (campos(l, c, 4) < 2) continue;
    r.linhas++;
    long s = segDoDia(c[0]);
    ult = s;
    const String& ev = c[1];
    auto fecha = [&]() {
      if (estado == 1) r.ligada += s - t;
      else if (estado == 0) r.parada += s - t;
    };
    if (ev == "CODIGO") r.codigos++;
    else if (ev == "FALHA_APAGAR") r.falhas++;
    else if (ev == "ESTEIRA_LIGADA") { fecha(); estado = 1; t = s; }
    else if (ev == "ESTEIRA_PARADA") { fecha(); estado = 0; t = s; }
    else if (ev == "INICIO") { estado = 0; t = s; }  // placa reiniciou: o que houve antes e desconhecido
    else if (ev == "DIA_INICIO") { estado = c[3] == "ligada" ? 1 : 0; t = s; }
    else if (ev == "HORA_ACERTADA") { estado = c[3] == "ligada" ? 1 : 0; t = s; }
    else if (ev == "DIA_FIM" && dia != "sem-hora") { fecha(); estado = -1; }
  }
  f.close();
  if (hoje && estado >= 0) {  // intervalo ainda aberto vai ate agora
    char d[16], h[10];
    carimbo(d, h);
    long s = segDoDia(h);
    if (estado == 1) r.ligada += s - t; else r.parada += s - t;
  }
  (void)ult;
  return r;
}

static void enviaCsv(EthernetClient& c, const String& dia) {
  File f = SD.open("/rel/" + dia + ".csv");
  if (!f) {
    c.print("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\nsem registros");
    return;
  }
  c.printf("HTTP/1.1 200 OK\r\nContent-Type: text/csv; charset=utf-8\r\nContent-Disposition: attachment; filename=\"%s.csv\"\r\n"
           "Connection: close\r\n\r\n",
           dia.c_str());
  uint8_t buf[512];
  int n;
  while ((n = f.read(buf, sizeof buf)) > 0) c.write(buf, n);
  f.close();
}
