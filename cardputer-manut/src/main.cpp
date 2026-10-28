// StamPLC Manut - firmware do M5Stack Cardputer que controla o StamPLC
// da Sumiriko sem precisar de notebook.
//
// Como usar:
//   1. Ligue o Cardputer. Tela principal com 6 icones.
//   2. ,  .    navegam entre icones (ou  < >  com shift)
//      ;  /    sobem/descem em listas
//      Enter   entra no menu/aplica valor
//      `       (crase, canto esquerdo) volta
//   3. Primeiro entre em WIFI > CONECTAR pra pareara com o AP do PLC.
//      Padrao: SSID StamPLC-Sumi senha sumi1234.
//      Depois disso as outras telas puxam dados do PLC via HTTP.
//
// Repo: github.com/Ramon-6/CodelineSS5632  autor: Ramon

#include <M5Cardputer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ---- Config padrao ---------------------------------------------------
#define AP_SSID_PADRAO   "StamPLC-Sumi"
#define AP_SENHA_PADRAO  "sumi1234"
#define BASE_URL         "http://192.168.4.1"

// Cores (fundo branco, letra preta)
#define C_BG   0xFFFF
#define C_FG   0x0000
#define C_MUT  0x8410    // cinza
#define C_OK   0x1F02    // verde
#define C_ER   0xF800    // vermelho
#define C_WK   0xFC00    // laranja
#define C_ACC  0x001F    // azul

// Resolucao do Cardputer
#define W 240
#define H 135

// ---- Estado global ---------------------------------------------------
static Preferences prefs;
static String ssid = AP_SSID_PADRAO;
static String senha = AP_SENHA_PADRAO;
static bool wifiOn = false;
static String ipPlc = "";          // IP do gateway (ap do PLC)
static String ipLocal = "";
static unsigned long tInput = 0;    // debounce basico de teclas

enum Tela : uint8_t {
  T_MENU, T_WIFI, T_WIFI_CONECTAR, T_WIFI_TROCAR,
  T_STATUS, T_AJUSTES, T_AJUSTE_EDIT, T_MODBUS, T_MB_RESULT,
  T_LOG, T_SOBRE
};
static Tela tela = T_MENU;
static int menuSel = 0;         // qual icone selecionado
static int listSel = 0;         // qual item da lista submenu
static int listOff = 0;         // scroll
static String msgErro = "";
static String ajusteChave = ""; // qual ajuste esta sendo editado
static long ajusteValor = 0;    // valor em edicao
static String ajusteUnidade = "s";
static String logConteudo = "aguarde...";
static String mbResultado = "";

// Modbus form
static int mbFC = 3;
static int mbAddr = 200;
static int mbN = 1;
static int mbCampo = 0;  // 0=fc 1=addr 2=n

// ---- Icones do menu principal ---------------------------------------
struct ItemMenu { const char* rotulo; };
static const ItemMenu MENU[] = {
  {"WIFI"}, {"STATUS"}, {"AJUSTES"},
  {"MODBUS"}, {"LOG"}, {"SOBRE"}
};
static const int MENU_N = 6;

// ---- Helpers de desenho ---------------------------------------------
// Double-buffering: tudo desenha num sprite 240x135 (~65 KB RAM) e so copia
// pra tela quando o quadro ta pronto. Elimina o piscar.
static M5Canvas canvas(&M5Cardputer.Display);
static auto& g() { return canvas; }

static void limpa() {
  g().fillScreen(C_BG);
  g().setTextColor(C_FG, C_BG);
}

static void flush() { canvas.pushSprite(0, 0); }

static void titulo(const char* t) {
  g().fillRect(0, 0, W, 18, C_FG);
  g().setTextColor(C_BG, C_FG);
  g().setFont(&fonts::Font2);
  g().setCursor(6, 2);
  g().print(t);
  // status WiFi no canto direito
  g().setCursor(W - 60, 2);
  g().print(wifiOn ? "WiFi OK" : "offline");
  g().setTextColor(C_FG, C_BG);
}

static void rodape(const char* esquerda, const char* direita) {
  g().setTextColor(C_MUT, C_BG);
  g().setFont(&fonts::Font0);
  g().setCursor(4, H - 10);
  g().print(esquerda);
  if (direita) {
    int lw = g().textWidth(direita);
    g().setCursor(W - lw - 4, H - 10);
    g().print(direita);
  }
  g().setTextColor(C_FG, C_BG);
  flush();  // double-buffer: envia o quadro inteiro de uma vez (sem piscar)
}

// ---- Icones vetoriais ------------------------------------------------
// Desenhados dentro de um retangulo (x,y, 40x30) ou (x,y, w,h)
static void iconeWifi(int cx, int cy) {
  for (int r = 14; r >= 6; r -= 4) {
    g().drawArc(cx, cy + 6, r, r - 2, 220, 320, C_FG);
  }
  g().fillCircle(cx, cy + 6, 2, C_FG);
}
static void iconeStatus(int x, int y) {
  g().drawRect(x, y, 32, 20, C_FG);
  // onda interna
  int bx = x + 2, by = y + 10;
  for (int i = 0; i < 28; i += 4) {
    g().drawLine(bx + i, by, bx + i + 2, by - 4, C_FG);
    g().drawLine(bx + i + 2, by - 4, bx + i + 4, by + 4, C_FG);
  }
}
static void iconeAjustes(int cx, int cy) {
  g().drawCircle(cx, cy, 10, C_FG);
  g().drawCircle(cx, cy, 4, C_FG);
  for (int a = 0; a < 360; a += 60) {
    float r1 = 10, r2 = 14;
    float rad = a * 3.14159f / 180.0f;
    g().drawLine(cx + cos(rad) * r1, cy + sin(rad) * r1,
                 cx + cos(rad) * r2, cy + sin(rad) * r2, C_FG);
  }
}
static void iconeModbus(int x, int y) {
  g().setFont(&fonts::Font4);
  g().setTextColor(C_FG, C_BG);
  g().setCursor(x, y);
  g().print("01");
  g().setCursor(x + 2, y + 14);
  g().print("10");
  g().setFont(&fonts::Font2);
}
static void iconeLog(int x, int y) {
  g().drawRect(x, y, 32, 22, C_FG);
  for (int i = 0; i < 4; i++) g().drawLine(x + 4, y + 4 + i * 4, x + 28, y + 4 + i * 4, C_FG);
}
static void iconeSobre(int cx, int cy) {
  g().drawCircle(cx, cy, 11, C_FG);
  g().setFont(&fonts::Font4);
  g().setTextColor(C_FG, C_BG);
  int w = g().textWidth("i");
  g().setCursor(cx - w / 2, cy - 10);
  g().print("i");
  g().setFont(&fonts::Font2);
}

static void desenhaIcone(int idx, int x, int y, int cw, int ch, bool sel) {
  if (sel) {
    g().fillRect(x, y, cw, ch, C_FG);
    g().setTextColor(C_BG, C_FG);
  } else {
    g().fillRect(x, y, cw, ch, C_BG);
    g().drawRect(x, y, cw, ch, C_MUT);
    g().setTextColor(C_FG, C_BG);
  }
  int cx = x + cw / 2, cy = y + 16;
  if (sel) {
    // Para o estado selecionado, redesenha os icones com fundo preto -> precisa
    // inverter as cores. Simplificacao: pinta o icone com um retangulo "massa"
    // branco com o shape em preto, nao da. Vou re-chamar com inversao manual:
    // na pratica os icones ja usam C_FG = preto, que no fundo preto some.
    // Entao desenho em branco:
    uint16_t fg = C_BG;
    (void)fg;
    // Workaround simples: inverter manualmente desenhando sobre o retangulo.
    // Para nao duplicar as funcoes, uso drawRect/drawCircle direto aqui.
    if (idx == 0) {  // WiFi
      for (int r = 14; r >= 6; r -= 4) g().drawArc(cx, cy + 6, r, r - 2, 220, 320, C_BG);
      g().fillCircle(cx, cy + 6, 2, C_BG);
    } else if (idx == 1) {  // Status
      int bx = cx - 16, by = cy - 10;
      g().drawRect(bx, by, 32, 20, C_BG);
      int wx = bx + 2, wy = by + 10;
      for (int i = 0; i < 28; i += 4) {
        g().drawLine(wx + i, wy, wx + i + 2, wy - 4, C_BG);
        g().drawLine(wx + i + 2, wy - 4, wx + i + 4, wy + 4, C_BG);
      }
    } else if (idx == 2) {  // Ajustes
      g().drawCircle(cx, cy, 10, C_BG);
      g().drawCircle(cx, cy, 4, C_BG);
      for (int a = 0; a < 360; a += 60) {
        float rad = a * 3.14159f / 180.0f;
        g().drawLine(cx + cos(rad) * 10, cy + sin(rad) * 10, cx + cos(rad) * 14, cy + sin(rad) * 14, C_BG);
      }
    } else if (idx == 3) {  // Modbus
      g().setFont(&fonts::Font4);
      g().setTextColor(C_BG, C_FG);
      g().setCursor(cx - 10, cy - 10);
      g().print("01");
      g().setCursor(cx - 8, cy + 4);
      g().print("10");
      g().setFont(&fonts::Font2);
      g().setTextColor(C_BG, C_FG);
    } else if (idx == 4) {  // Log
      int bx = cx - 16, by = cy - 11;
      g().drawRect(bx, by, 32, 22, C_BG);
      for (int i = 0; i < 4; i++) g().drawLine(bx + 4, by + 4 + i * 4, bx + 28, by + 4 + i * 4, C_BG);
    } else if (idx == 5) {  // Sobre
      g().drawCircle(cx, cy, 11, C_BG);
      g().setFont(&fonts::Font4);
      g().setTextColor(C_BG, C_FG);
      int w = g().textWidth("i");
      g().setCursor(cx - w / 2, cy - 10);
      g().print("i");
      g().setFont(&fonts::Font2);
      g().setTextColor(C_BG, C_FG);
    }
  } else {
    if (idx == 0) iconeWifi(cx, cy);
    else if (idx == 1) iconeStatus(cx - 16, cy - 10);
    else if (idx == 2) iconeAjustes(cx, cy);
    else if (idx == 3) iconeModbus(cx - 10, cy - 10);
    else if (idx == 4) iconeLog(cx - 16, cy - 11);
    else if (idx == 5) iconeSobre(cx, cy);
  }
  // rotulo
  g().setFont(&fonts::Font2);
  int rw = g().textWidth(MENU[idx].rotulo);
  g().setCursor(x + (cw - rw) / 2, y + ch - 14);
  g().print(MENU[idx].rotulo);
  g().setTextColor(C_FG, C_BG);
}

// Desenha um icone grande (60px) centralizado em (cx, cy)
static void iconeGrande(int idx, int cx, int cy) {
  if (idx == 0) {  // WIFI
    for (int r = 36; r >= 12; r -= 10) {
      g().drawArc(cx, cy + 18, r, r - 3, 220, 320, C_FG);
    }
    g().fillCircle(cx, cy + 18, 5, C_FG);
  } else if (idx == 1) {  // STATUS
    int bw = 64, bh = 40;
    int bx = cx - bw / 2, by = cy - bh / 2;
    g().drawRect(bx, by, bw, bh, C_FG);
    g().drawRect(bx + 1, by + 1, bw - 2, bh - 2, C_FG);
    int wy = cy + 4;
    for (int i = 0; i < 58; i += 6) {
      g().drawLine(bx + 3 + i, wy, bx + 3 + i + 3, wy - 10, C_FG);
      g().drawLine(bx + 3 + i + 3, wy - 10, bx + 3 + i + 6, wy + 8, C_FG);
    }
  } else if (idx == 2) {  // AJUSTES (engrenagem)
    g().fillCircle(cx, cy, 22, C_FG);
    g().fillCircle(cx, cy, 10, C_BG);
    for (int a = 0; a < 360; a += 45) {
      float rad = a * 3.14159f / 180.0f;
      int x1 = cx + cos(rad) * 20, y1 = cy + sin(rad) * 20;
      int x2 = cx + cos(rad) * 30, y2 = cy + sin(rad) * 30;
      g().fillCircle(x2, y2, 5, C_FG);
      (void)x1; (void)y1;
    }
    g().fillCircle(cx, cy, 10, C_BG);
    g().drawCircle(cx, cy, 10, C_FG);
  } else if (idx == 3) {  // MODBUS
    g().setFont(&fonts::Font7);
    g().setTextColor(C_FG, C_BG);
    const char* t = "01";
    int tw = g().textWidth(t);
    g().setCursor(cx - tw / 2, cy - 24);
    g().print(t);
    g().setFont(&fonts::Font2);
  } else if (idx == 4) {  // LOG
    int bw = 68, bh = 50;
    int bx = cx - bw / 2, by = cy - bh / 2;
    g().drawRect(bx, by, bw, bh, C_FG);
    g().drawRect(bx + 1, by + 1, bw - 2, bh - 2, C_FG);
    for (int i = 0; i < 5; i++) {
      g().drawLine(bx + 6, by + 8 + i * 8, bx + bw - 6, by + 8 + i * 8, C_FG);
      g().drawLine(bx + 6, by + 9 + i * 8, bx + bw - 6, by + 9 + i * 8, C_FG);
    }
  } else if (idx == 5) {  // SOBRE (i no circulo)
    g().fillCircle(cx, cy, 26, C_FG);
    g().fillCircle(cx, cy, 23, C_BG);
    g().setFont(&fonts::Font7);
    g().setTextColor(C_FG, C_BG);
    const char* t = "i";
    int tw = g().textWidth(t);
    g().setCursor(cx - tw / 2, cy - 24);
    g().print(t);
    g().setFont(&fonts::Font2);
  }
}

// ---- Tela: menu principal (1 icone por vez) -------------------------
static void desenhaMenu() {
  limpa();
  titulo("StamPLC Manut");

  // Contador "N de 6" no canto direito do topo (sobre o status WiFi) nao, poe centrado
  g().setFont(&fonts::Font2);
  g().setTextColor(C_MUT, C_BG);
  char cnt[12]; snprintf(cnt, sizeof cnt, "%d / %d", menuSel + 1, MENU_N);
  int cw = g().textWidth(cnt);
  g().setCursor((W - cw) / 2, 22);
  g().print(cnt);
  g().setTextColor(C_FG, C_BG);

  // Icone grande centralizado
  int cx = W / 2, cy = 70;
  iconeGrande(menuSel, cx, cy);

  // Rotulo bem grande embaixo
  g().setFont(&fonts::Font4);
  const char* rot = MENU[menuSel].rotulo;
  int rw = g().textWidth(rot);
  g().setCursor((W - rw) / 2, 108);
  g().print(rot);
  g().setFont(&fonts::Font2);

  // Setas laterais indicando navegacao
  g().setTextColor(C_MUT, C_BG);
  g().setFont(&fonts::Font4);
  g().setCursor(4, 60);
  g().print("<");
  int ww = g().textWidth(">");
  g().setCursor(W - ww - 4, 60);
  g().print(">");
  g().setFont(&fonts::Font2);
  g().setTextColor(C_FG, C_BG);

  rodape("< > navega  Enter", "entrar");
}

// ---- HTTP helpers ----------------------------------------------------
static int httpGet(const String& url, String& body) {
  if (!wifiOn) return -1;
  HTTPClient http;
  http.setTimeout(3000);
  http.begin(url);
  int code = http.GET();
  if (code == 200) body = http.getString();
  http.end();
  return code;
}

// ---- Tela: WiFi submenu ---------------------------------------------
static const char* ITENS_WIFI[] = {"CONECTAR", "TROCAR REDE", "DESCONECTAR"};
static const int ITENS_WIFI_N = 3;

static void desenhaWifi() {
  limpa();
  titulo("WIFI");
  g().setFont(&fonts::Font2);
  g().setCursor(6, 24);
  g().print("SSID: "); g().print(ssid);
  g().setCursor(6, 40);
  g().print("Status: ");
  g().setTextColor(wifiOn ? C_OK : C_ER, C_BG);
  g().print(wifiOn ? "conectado" : "desligado");
  g().setTextColor(C_FG, C_BG);
  if (wifiOn) {
    g().setCursor(6, 56);
    g().print("IP: "); g().print(ipLocal);
    g().setCursor(6, 70);
    g().print("PLC: "); g().print(ipPlc);
  }
  for (int i = 0; i < ITENS_WIFI_N; i++) {
    int y = 86 + i * 14;
    if (i == listSel) {
      g().fillRect(4, y - 1, W - 8, 13, C_FG);
      g().setTextColor(C_BG, C_FG);
    } else {
      g().setTextColor(C_FG, C_BG);
    }
    g().setCursor(10, y);
    g().print("> ");
    g().print(ITENS_WIFI[i]);
  }
  g().setTextColor(C_FG, C_BG);
  rodape("; / up/dn  Enter OK", "` voltar");
}

static void acaoWifiConectar() {
  tela = T_WIFI_CONECTAR;
  limpa();
  titulo("CONECTANDO");
  g().setCursor(6, 30);
  g().setFont(&fonts::Font2);
  g().print("SSID: "); g().print(ssid);
  g().setCursor(6, 46);
  g().print("tentando...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), senha.c_str());
  unsigned long t0 = millis();
  int dots = 0;
  flush();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(300);
    g().setCursor(6 + 80 + dots * 6, 46);
    g().print(".");
    dots = (dots + 1) % 10;
    if (dots == 0) {
      g().fillRect(80, 46, 60, 16, C_BG);
    }
    flush();
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiOn = true;
    ipLocal = WiFi.localIP().toString();
    ipPlc = WiFi.gatewayIP().toString();
    g().setCursor(6, 70);
    g().setTextColor(C_OK, C_BG);
    g().print("CONECTADO");
    g().setTextColor(C_FG, C_BG);
    g().setCursor(6, 86);
    g().print("IP: "); g().print(ipLocal);
    g().setCursor(6, 100);
    g().print("PLC: "); g().print(ipPlc);
  } else {
    wifiOn = false;
    g().setCursor(6, 70);
    g().setTextColor(C_ER, C_BG);
    g().print("FALHOU");
    g().setTextColor(C_FG, C_BG);
    g().setCursor(6, 86);
    g().print("verifique: AP ligado?");
    g().setCursor(6, 100);
    g().print("segure A no PLC por 3s");
  }
  rodape("`  voltar", nullptr);
}

static void acaoWifiDesconectar() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiOn = false;
  ipLocal = ""; ipPlc = "";
  tela = T_WIFI; listSel = 0;
}

// ---- Tela: trocar rede (edicao SSID + senha) ------------------------
static int trocarCampo = 0;        // 0=ssid 1=senha
static String trocarSSID = "";
static String trocarSenha = "";

static void desenhaTrocar() {
  limpa();
  titulo("TROCAR REDE");
  g().setFont(&fonts::Font2);
  g().setCursor(6, 26); g().print("SSID (digite, Enter avanca):");
  g().drawRect(6, 42, W - 12, 18, trocarCampo == 0 ? C_FG : C_MUT);
  g().setCursor(10, 46);
  g().print(trocarSSID);
  if (trocarCampo == 0) { g().print("_"); }
  g().setCursor(6, 68); g().print("Senha:");
  g().drawRect(6, 82, W - 12, 18, trocarCampo == 1 ? C_FG : C_MUT);
  g().setCursor(10, 86);
  g().print(trocarSenha);
  if (trocarCampo == 1) { g().print("_"); }
  rodape("digite  Enter avanca", "` cancela");
}

static void aplicarTrocarRede() {
  if (trocarSSID.length() == 0) { msgErro = "SSID vazio"; return; }
  ssid = trocarSSID;
  senha = trocarSenha;
  prefs.putString("ssid", ssid);
  prefs.putString("senha", senha);
}

// ---- Tela: Status ao vivo (polling /live.json) ----------------------
static JsonDocument statusDoc;
static unsigned long tStatusUlt = 0;

static void atualizaStatus() {
  String body;
  int code = httpGet(String(BASE_URL) + "/live.json", body);
  if (code != 200) { msgErro = "GET /live.json: " + String(code); return; }
  DeserializationError e = deserializeJson(statusDoc, body);
  if (e) { msgErro = String("JSON: ") + e.c_str(); return; }
  msgErro = "";
}

static void desenhaStatus() {
  limpa();
  titulo("STATUS");
  if (!wifiOn) {
    g().setCursor(6, 40); g().print("conecte o WiFi primeiro");
    rodape("`  voltar", nullptr);
    return;
  }
  if (msgErro.length()) {
    g().setTextColor(C_ER, C_BG);
    g().setCursor(6, 40); g().print(msgErro);
    g().setTextColor(C_FG, C_BG);
    rodape("Enter tentar  `  voltar", nullptr);
    return;
  }
  g().setFont(&fonts::Font2);
  const char* fase = statusDoc["fase"] | "-";
  // destaque da fase
  uint16_t cor = C_WK;
  if (!strcmp(fase, "OCIOSO")) cor = C_OK;
  else if (!strcmp(fase, "ERRO")) cor = C_ER;
  g().fillRect(0, 22, W, 20, cor);
  g().setTextColor(C_BG, cor);
  g().setFont(&fonts::Font4);
  g().setCursor(6, 24);
  g().print(fase);
  g().setTextColor(C_FG, C_BG);
  g().setFont(&fonts::Font2);
  int y = 48;
  g().setCursor(6, y); g().print("Codigo: ");
  const char* cod = statusDoc["codigo"] | "-";
  g().print(cod[0] ? cod : "-"); y += 13;
  g().setCursor(6, y); g().printf("Modo: %s", (const char*)(statusDoc["modo"] | "?")); y += 13;
  g().setCursor(6, y); g().printf("Tempo: %lus de %lus",
    (unsigned long)(statusDoc["tempo_restante_s"] | 0),
    (unsigned long)(statusDoc["tempo_alvo_s"] | 0));
  y += 13;
  g().setCursor(6, y);
  bool rele = statusDoc["rele"] | false;
  bool est = statusDoc["esteira"] | false;
  g().printf("Rele:%s  Esteira:%s  Enc:%lu",
    rele ? "ON" : "off", est ? "ON" : "off",
    (unsigned long)(statusDoc["encoder"] | 0));
  rodape("atualiza 1.5s", "` voltar");
}

// ---- Tela: Ajustes (lista) ------------------------------------------
struct ItemAjuste { const char* rotulo; const char* chave; const char* unidade; int tipo; };
// tipo: 0 = numero (segundos), 1 = toggle 0/1, 2 = enum auto/manual, 3 = acao
static const ItemAjuste AJUSTES[] = {
  {"Tempo padrao",  "tempo",  "s",       0},
  {"MIX A",         "mixa",   "s",       0},
  {"MIX B",         "mixb",   "s",       0},
  {"Espera operador","espera","s",       0},
  {"Modo",          "modo",   "",        2},
  {"Sirene",        "sirene", "",        1},
  {"Encoder fim",   "encF",   "",        1},
  {"TESTAR ciclo",  "acao",   "testar",  3},
  {"PARAR ciclo",   "acao",   "parar",   3},
};
static const int AJUSTES_N = 9;

static long valorAtual(const ItemAjuste& a) {
  if (!strcmp(a.chave, "tempo"))  return (long)(statusDoc["tempo_alvo_s"] | 0);
  if (!strcmp(a.chave, "mixa"))   return (long)(statusDoc["tempo_mixa_s"] | 0);
  if (!strcmp(a.chave, "mixb"))   return (long)(statusDoc["tempo_mixb_s"] | 0);
  if (!strcmp(a.chave, "espera")) return 60;
  if (!strcmp(a.chave, "sirene")) return (long)(statusDoc["sirene_hab"] | 0);
  if (!strcmp(a.chave, "encF"))   return (long)(statusDoc["encoder_fim_hab"] | 0);
  return 0;
}

static void desenhaAjustes() {
  limpa();
  titulo("AJUSTES");
  if (!wifiOn) {
    g().setCursor(6, 40); g().print("conecte o WiFi primeiro");
    rodape("`  voltar", nullptr);
    return;
  }
  g().setFont(&fonts::Font2);
  // 5 itens visiveis por vez (115/13 ~= 8)
  int VIS = 7;
  if (listSel < listOff) listOff = listSel;
  if (listSel >= listOff + VIS) listOff = listSel - VIS + 1;
  for (int i = 0; i < VIS && listOff + i < AJUSTES_N; i++) {
    int idx = listOff + i;
    int y = 22 + i * 14;
    bool sel = idx == listSel;
    if (sel) { g().fillRect(0, y - 1, W, 13, C_FG); g().setTextColor(C_BG, C_FG); }
    else     { g().setTextColor(C_FG, C_BG); }
    g().setCursor(6, y);
    g().print(AJUSTES[idx].rotulo);
    // mostra valor atual
    char val[20] = "";
    if (AJUSTES[idx].tipo == 0) snprintf(val, sizeof val, "%lds", valorAtual(AJUSTES[idx]));
    else if (AJUSTES[idx].tipo == 1) snprintf(val, sizeof val, "%s", valorAtual(AJUSTES[idx]) ? "ON" : "off");
    else if (AJUSTES[idx].tipo == 2) {
      const char* m = statusDoc["modo"] | "?";
      snprintf(val, sizeof val, "%s", m);
    }
    int vw = g().textWidth(val);
    g().setCursor(W - vw - 6, y);
    g().print(val);
  }
  g().setTextColor(C_FG, C_BG);
  rodape("; /  Enter editar", "` voltar");
}

// ---- Tela: editar valor de ajuste ----------------------------------
static void desenhaAjusteEdit() {
  limpa();
  const ItemAjuste& a = AJUSTES[listSel];
  titulo(a.rotulo);
  g().setFont(&fonts::Font2);
  g().setCursor(6, 30);
  if (a.tipo == 0) {
    g().print("Valor em segundos:");
    g().setFont(&fonts::Font4);
    char v[16];
    snprintf(v, sizeof v, "%ld s", ajusteValor);
    g().setCursor(6, 50);
    g().print(v);
    g().setFont(&fonts::Font2);
    g().setCursor(6, 80);
    g().print(". +10   , -10   / +60   ; -60");
  } else if (a.tipo == 1) {
    g().print("Toggle (atual: ");
    g().print(ajusteValor ? "ON" : "off");
    g().print(")");
    g().setFont(&fonts::Font4);
    g().setCursor(6, 60);
    g().print(ajusteValor ? "ON" : "OFF");
    g().setFont(&fonts::Font2);
    g().setCursor(6, 90);
    g().print(". inverte");
  } else if (a.tipo == 2) {
    g().print("Modo:");
    g().setFont(&fonts::Font4);
    g().setCursor(6, 60);
    g().print(ajusteValor ? "MANUAL" : "AUTO");
    g().setFont(&fonts::Font2);
    g().setCursor(6, 90);
    g().print(". inverte");
  } else if (a.tipo == 3) {
    g().print("Confirmar acao:");
    g().setFont(&fonts::Font4);
    g().setCursor(6, 60);
    g().print(a.unidade);
    g().setFont(&fonts::Font2);
  }
  if (msgErro.length()) {
    g().setTextColor(C_ER, C_BG);
    g().setCursor(6, 110);
    g().print(msgErro);
    g().setTextColor(C_FG, C_BG);
  }
  rodape("Enter aplicar", "` cancelar");
}

static void aplicarAjuste() {
  const ItemAjuste& a = AJUSTES[listSel];
  String url = String(BASE_URL) + "/set?";
  if (a.tipo == 0) { url += a.chave; url += "="; url += ajusteValor; }
  else if (a.tipo == 1) { url += a.chave; url += "="; url += (ajusteValor ? "1" : "0"); }
  else if (a.tipo == 2) { url += "modo="; url += (ajusteValor ? "manual" : "auto"); }
  else if (a.tipo == 3) { url += "acao="; url += a.unidade; }
  String body;
  int code = httpGet(url, body);
  if (code == 200) {
    msgErro = "";
    tela = T_AJUSTES;
  } else {
    msgErro = "HTTP " + String(code);
  }
}

// ---- Tela: Modbus explorer -----------------------------------------
static void desenhaModbus() {
  limpa();
  titulo("MODBUS");
  if (!wifiOn) {
    g().setCursor(6, 40); g().print("conecte o WiFi primeiro");
    rodape("`  voltar", nullptr);
    return;
  }
  g().setFont(&fonts::Font2);
  const char* nomes[] = {"3-holdR","4-inpR","1-coilR","2-discR","6-holdW","5-coilW"};
  static const int fcs[] = {3,4,1,2,6,5};
  int fcIdx = 0; for (int i=0;i<6;i++) if (fcs[i]==mbFC) fcIdx = i;
  const char* fcNome = nomes[fcIdx];

  g().setCursor(6, 26);
  g().print("Funcao : ");
  if (mbCampo == 0) { g().fillRect(60, 25, W-66, 13, C_FG); g().setTextColor(C_BG, C_FG); }
  g().setCursor(64, 26); g().print(fcNome);
  g().setTextColor(C_FG, C_BG);

  g().setCursor(6, 42);
  g().print("Endrc  : ");
  if (mbCampo == 1) { g().fillRect(60, 41, W-66, 13, C_FG); g().setTextColor(C_BG, C_FG); }
  g().setCursor(64, 42); g().print(mbAddr);
  g().setTextColor(C_FG, C_BG);

  g().setCursor(6, 58);
  g().print(mbFC == 5 || mbFC == 6 ? "Valor  : " : "N regs : ");
  if (mbCampo == 2) { g().fillRect(60, 57, W-66, 13, C_FG); g().setTextColor(C_BG, C_FG); }
  g().setCursor(64, 58); g().print(mbN);
  g().setTextColor(C_FG, C_BG);

  g().setCursor(6, 80);
  g().setTextColor(C_MUT, C_BG);
  g().print("Uteis: 200 scanner  60 enc");
  g().setCursor(6, 94);
  g().print("       17 tampa  2 clear");
  g().setTextColor(C_FG, C_BG);
  rodape("; / troca  . , muda", "Enter exec  ` volta");
}

static void execModbus() {
  String url = String(BASE_URL) + "/mb?fc=" + mbFC + "&a=" + mbAddr;
  if (mbFC == 5 || mbFC == 6) url += "&v=" + String(mbN);
  else url += "&n=" + String(mbN);
  String body;
  int code = httpGet(url, body);
  if (code != 200) { mbResultado = "HTTP " + String(code); tela = T_MB_RESULT; return; }
  mbResultado = body;
  tela = T_MB_RESULT;
}

static int mbScroll = 0;
static void desenhaMbResult() {
  limpa();
  titulo("MB RESULTADO");
  g().setFont(&fonts::Font0);
  // quebra o json em linhas de ~40 chars
  int y = 22;
  int i = mbScroll;
  while (i < (int)mbResultado.length() && y < H - 14) {
    int fim = i + 40;
    if (fim > (int)mbResultado.length()) fim = mbResultado.length();
    g().setCursor(4, y);
    g().print(mbResultado.substring(i, fim));
    y += 10;
    i = fim;
  }
  rodape("; / scroll", "` voltar");
}

// ---- Tela: Log ------------------------------------------------------
static int logScroll = 0;
static void atualizaLog() {
  String body;
  int code = httpGet(String(BASE_URL) + "/log.txt", body);
  if (code == 200) logConteudo = body;
  else logConteudo = "HTTP " + String(code);
}

static void desenhaLog() {
  limpa();
  titulo("LOG");
  if (!wifiOn) {
    g().setCursor(6, 40); g().print("conecte o WiFi primeiro");
    rodape("`  voltar", nullptr);
    return;
  }
  g().setFont(&fonts::Font0);
  // mostra ate ~11 linhas
  int y = 20;
  int lin = 0;
  int i = 0;
  // pula logScroll linhas
  int pulos = 0;
  while (pulos < logScroll && i < (int)logConteudo.length()) {
    if (logConteudo[i] == '\n') pulos++;
    i++;
  }
  while (i < (int)logConteudo.length() && lin < 11) {
    int fim = logConteudo.indexOf('\n', i);
    if (fim < 0) fim = logConteudo.length();
    String l = logConteudo.substring(i, fim);
    if (l.length() > 40) l = l.substring(0, 40);
    g().setCursor(4, y);
    g().print(l);
    y += 10; lin++;
    i = fim + 1;
  }
  rodape("; / scroll  Enter atualiza", "` voltar");
}

// ---- Tela: Sobre ----------------------------------------------------
static void desenhaSobre() {
  limpa();
  titulo("SOBRE");
  g().setFont(&fonts::Font2);
  int y = 28;
  g().setCursor(6, y); g().print("StamPLC Manut v1.0"); y += 16;
  g().setCursor(6, y); g().print("Ramon / Sumiriko"); y += 16;
  g().setCursor(6, y); g().print("Cardputer <-> StamPLC"); y += 16;
  g().setTextColor(C_ACC, C_BG);
  g().setCursor(6, y); g().print("github.com/Ramon-6/"); y += 14;
  g().setCursor(6, y); g().print("CodelineSS5632");
  g().setTextColor(C_FG, C_BG);
  rodape("", "` voltar");
}

// ---- Input handling -------------------------------------------------
struct Teclas { bool left, right, up, down, enter, esc, back; String digitos; };

static Teclas lerTeclas() {
  Teclas t = {};
  if (!M5Cardputer.Keyboard.isChange()) return t;
  if (!M5Cardputer.Keyboard.isPressed()) return t;
  Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
  t.enter = st.enter;
  t.back = st.del;
  for (auto c : st.word) {
    if (c == ',' || c == '<') t.left = true;
    else if (c == '.' || c == '>') t.right = true;
    else if (c == ';' || c == ':') t.up = true;
    else if (c == '/' || c == '?') t.down = true;
    else if (c == '`' || c == '~') t.esc = true;
    else t.digitos += c;
  }
  return t;
}

// ---- Loop principal -------------------------------------------------
static unsigned long tTick = 0;

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(C_BG);

  // Sprite 240x135 em 16bpp (~65 KB RAM). Precisa pedir PSRAM? S3 tem 320 KB
  // SRAM, nao precisa. createSprite usa heap do SRAM.
  canvas.setColorDepth(16);
  canvas.createSprite(W, H);
  canvas.fillScreen(C_BG);
  canvas.setTextColor(C_FG, C_BG);
  canvas.setFont(&fonts::Font2);

  prefs.begin("manut", false);
  ssid = prefs.getString("ssid", AP_SSID_PADRAO);
  senha = prefs.getString("senha", AP_SENHA_PADRAO);

  desenhaMenu();
}

void loop() {
  M5Cardputer.update();
  Teclas t = lerTeclas();

  // ---- Polling periodico de dados ao vivo ----
  if (wifiOn && (tela == T_STATUS || tela == T_AJUSTES) && millis() - tTick > 1500) {
    tTick = millis();
    atualizaStatus();
    if (tela == T_STATUS) desenhaStatus();
    else desenhaAjustes();
  }

  // ---- Processa input por tela ----
  switch (tela) {
    case T_MENU: {
      if (t.left || t.up)   { menuSel = (menuSel + MENU_N - 1) % MENU_N; desenhaMenu(); }
      if (t.right || t.down) { menuSel = (menuSel + 1) % MENU_N; desenhaMenu(); }
      if (t.enter) {
        listSel = 0; listOff = 0; msgErro = "";
        if (menuSel == 0) { tela = T_WIFI; desenhaWifi(); }
        else if (menuSel == 1) { tela = T_STATUS; atualizaStatus(); desenhaStatus(); }
        else if (menuSel == 2) { tela = T_AJUSTES; atualizaStatus(); desenhaAjustes(); }
        else if (menuSel == 3) { tela = T_MODBUS; mbCampo = 0; desenhaModbus(); }
        else if (menuSel == 4) { tela = T_LOG; logScroll = 0; atualizaLog(); desenhaLog(); }
        else if (menuSel == 5) { tela = T_SOBRE; desenhaSobre(); }
      }
    } break;

    case T_WIFI: {
      if (t.up)   { listSel = (listSel + ITENS_WIFI_N - 1) % ITENS_WIFI_N; desenhaWifi(); }
      if (t.down) { listSel = (listSel + 1) % ITENS_WIFI_N; desenhaWifi(); }
      if (t.enter) {
        if (listSel == 0) acaoWifiConectar();
        else if (listSel == 1) { trocarCampo = 0; trocarSSID = ""; trocarSenha = ""; tela = T_WIFI_TROCAR; desenhaTrocar(); }
        else if (listSel == 2) { acaoWifiDesconectar(); desenhaWifi(); }
      }
      if (t.esc) { tela = T_MENU; desenhaMenu(); }
    } break;

    case T_WIFI_CONECTAR: {
      if (t.esc || t.enter) { tela = T_WIFI; desenhaWifi(); }
    } break;

    case T_WIFI_TROCAR: {
      if (t.esc) { tela = T_WIFI; desenhaWifi(); break; }
      if (t.enter) {
        if (trocarCampo == 0) { trocarCampo = 1; desenhaTrocar(); }
        else { aplicarTrocarRede(); tela = T_WIFI; desenhaWifi(); }
        break;
      }
      if (t.back) {
        String& s = (trocarCampo == 0 ? trocarSSID : trocarSenha);
        if (s.length() > 0) s.remove(s.length() - 1);
        desenhaTrocar();
      }
      if (t.digitos.length()) {
        String& s = (trocarCampo == 0 ? trocarSSID : trocarSenha);
        if (s.length() < 31) s += t.digitos;
        desenhaTrocar();
      }
    } break;

    case T_STATUS: {
      if (t.esc) { tela = T_MENU; desenhaMenu(); }
      if (t.enter) { atualizaStatus(); desenhaStatus(); }
    } break;

    case T_AJUSTES: {
      if (t.up)   { listSel = (listSel + AJUSTES_N - 1) % AJUSTES_N; desenhaAjustes(); }
      if (t.down) { listSel = (listSel + 1) % AJUSTES_N; desenhaAjustes(); }
      if (t.enter) {
        const ItemAjuste& a = AJUSTES[listSel];
        if (a.tipo == 2) {
          const char* m = statusDoc["modo"] | "auto";
          ajusteValor = !strcmp(m, "manual") ? 1 : 0;
        } else ajusteValor = valorAtual(a);
        msgErro = "";
        tela = T_AJUSTE_EDIT;
        desenhaAjusteEdit();
      }
      if (t.esc) { tela = T_MENU; desenhaMenu(); }
    } break;

    case T_AJUSTE_EDIT: {
      const ItemAjuste& a = AJUSTES[listSel];
      if (t.esc) { tela = T_AJUSTES; desenhaAjustes(); break; }
      if (t.enter) { aplicarAjuste(); if (tela == T_AJUSTES) desenhaAjustes(); else desenhaAjusteEdit(); break; }
      if (a.tipo == 0) {
        if (t.right) ajusteValor += 10;
        if (t.left)  ajusteValor -= 10;
        if (t.down)  ajusteValor += 60;
        if (t.up)    ajusteValor -= 60;
        if (ajusteValor < 60) ajusteValor = 60;
        if (ajusteValor > 36000) ajusteValor = 36000;
      } else if (a.tipo == 1 || a.tipo == 2) {
        if (t.right || t.left || t.up || t.down) ajusteValor = !ajusteValor;
      }
      if (t.right || t.left || t.up || t.down) desenhaAjusteEdit();
    } break;

    case T_MODBUS: {
      static const int fcs[] = {3,4,1,2,6,5};
      if (t.esc) { tela = T_MENU; desenhaMenu(); break; }
      if (t.up || t.down) { mbCampo = (mbCampo + (t.up ? 2 : 1)) % 3; desenhaModbus(); break; }
      if (t.left || t.right) {
        int d = t.right ? 1 : -1;
        if (mbCampo == 0) {
          int idx = 0; for (int i=0;i<6;i++) if (fcs[i]==mbFC) idx = i;
          idx = (idx + d + 6) % 6;
          mbFC = fcs[idx];
        } else if (mbCampo == 1) {
          mbAddr += d;
          if (mbAddr < 0) mbAddr = 0;
          if (mbAddr > 65535) mbAddr = 65535;
        } else {
          mbN += d;
          if (mbN < 1) mbN = 1;
          if (mbN > 256) mbN = 256;
        }
        desenhaModbus();
      }
      if (t.enter) { execModbus(); if (tela == T_MB_RESULT) desenhaMbResult(); }
    } break;

    case T_MB_RESULT: {
      if (t.esc) { tela = T_MODBUS; desenhaModbus(); }
      if (t.up) { mbScroll = max(0, mbScroll - 40); desenhaMbResult(); }
      if (t.down) { mbScroll += 40; desenhaMbResult(); }
    } break;

    case T_LOG: {
      if (t.esc) { tela = T_MENU; desenhaMenu(); }
      if (t.enter) { atualizaLog(); logScroll = 0; desenhaLog(); }
      if (t.up) { logScroll = max(0, logScroll - 1); desenhaLog(); }
      if (t.down) { logScroll++; desenhaLog(); }
    } break;

    case T_SOBRE: {
      if (t.esc || t.enter) { tela = T_MENU; desenhaMenu(); }
    } break;
  }
  delay(10);
}
