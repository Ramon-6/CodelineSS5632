#pragma once
// Texto de status (usado na tela e na web) e as 4 telas do display do StamPLC.
// Incluir no main.cpp depois do bloco de estado.

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
static const uint16_t C_LOGO_BG = 0xFFFF, C_LOGO_RED = RGB565(214, 40, 40), C_LOGO_GRAY = RGB565(110, 114, 124);
static const uint16_t C_BG = RGB565(246, 247, 250), C_BAR = 0xFFFF, C_CARD = 0xFFFF,
                      C_SEP = RGB565(220, 224, 232),
                      C_OK = RGB565(16, 136, 68), C_OKBG = RGB565(220, 243, 227),
                      C_WORK = RGB565(185, 115, 0), C_WORKBG = RGB565(254, 240, 203),
                      C_ERR = RGB565(192, 32, 32), C_ERRBG = RGB565(252, 219, 219),
                      C_TXT = RGB565(28, 32, 42), C_DIM = RGB565(110, 118, 134);

// nivel: 0 = tudo certo, 1 = trabalhando, 2 = problema
struct Status {
  const char* titulo;
  const char* detalhe;
  uint16_t cor;
  int nivel;
};
static char detBuf[40];

static Status statusAtual() {
  if (fase == RODANDO) {
    long resta = ((long)esteiraSegMs - (long)(millis() - tFase) + 999) / 1000;
    if (resta < 0) resta = 0;
    snprintf(detBuf, sizeof detBuf, "Esteira rodando, restam %ld s", resta);
    return {"ESTEIRA RODANDO", detBuf, C_OK, 1};
  }
  if (fase == PRE_PARA_PULSA || fase == PRE_PARA_ESPERA)
    return {"AJUSTANDO", "Esteira ja rodava, parando antes", C_WORK, 1};
  if (fase == PULSA_LIGA || fase == AGUARDA_LIGA)
    return {"LIGANDO ESTEIRA", "Pulsando o botao...", C_WORK, 1};
  if (fase == PULSA_PARA || fase == AGUARDA_PARA)
    return {"PARANDO ESTEIRA", "Pulsando o botao...", C_WORK, 1};
  if (fase == ERRO) return {"ERRO NO CICLO", erroMsg, C_ERR, 2};
  if (!linkOk) return {"SEM REDE", "Cabo de rede desconectado", C_ERR, 2};
  if (fase == LIMPANDO) {
    if (!online) return {"SEM IMPRESSORA", "Nao consigo apagar o codigo", C_ERR, 2};
    if (falhaLimpeza) return {"NAO APAGOU", "Tentando de novo...", C_ERR, 2};
    return {"APAGANDO", "Limpando codigo da impressora", C_WORK, 1};
  }
  if (!online) return {"SEM IMPRESSORA", "Verifique cabo e IP (tela 3)", C_ERR, 2};
  return {"PRONTO", "Aguardando leitura do codigo", C_OK, 0};
}

// ---- Telas -------------------------------------------------------------------
static int pagina = 0;  // 0 painel, 1 reles, 2 rede, 3 eventos
#define N_PAG 5
static const char* NOME_PAG[N_PAG] = {"PAINEL", "RELES", "ESTADOS", "EVENTOS", "AJUSTES"};
static unsigned long tSalvo = 0;  // quando o ultimo ajuste foi salvo (para o aviso 'Salvo')

static String esteiraDesc() {
  char b[48];
  if (esteiraEntrada < 0) snprintf(b, sizeof b, "segue o rele R%d", RELE_CANAL + 1);
  else snprintf(b, sizeof b, "entrada %d (%s)", esteiraEntrada + 1, esteiraAlto ? "com tensao" : "sem tensao");
  return b;
}
static M5Canvas* cv = nullptr;  // desenha fora da tela e envia de uma vez: sem piscar

static String durCurta(long s) {
  char b[16];
  if (s >= 3600) snprintf(b, sizeof b, "%ldh%02ld", s / 3600, (s / 60) % 60);
  else if (s >= 60) snprintf(b, sizeof b, "%ldmin", s / 60);
  else snprintf(b, sizeof b, "%lds", s);
  return b;
}

// escreve texto cortado em maxc caracteres. fundo transparente para nao criar "marca-texto".
static void txt(M5Canvas& g, int x, int y, const char* s, uint16_t cor, int maxc = 34) {
  char b[48];
  snprintf(b, sizeof b, "%.*s", maxc, s);
  g.setCursor(x, y);
  g.setTextColor(cor);  // sem bg: as letras nao pintam um retangulo atras
  g.print(b);
}

// Marca: CARGA (preto) POSITIVA (vermelho) com fonte mais grossa. Retorna onde termina.
static int desenhaLogo(M5Canvas& g) {
  g.setFont(&fonts::Font4);  // fonte maior e mais grossa que Font2
  int x = 6, y = 2;
  g.setTextColor(TFT_BLACK, C_LOGO_BG);
  // multipass para engrossar
  for (int dy = 0; dy <= 1; dy++)
    for (int dx = 0; dx <= 1; dx++) {
      g.setCursor(x + dx, y + dy);
      g.print("CARGA ");
    }
  int x2 = x + g.textWidth("CARGA ");
  g.setTextColor(C_LOGO_RED, C_LOGO_BG);
  for (int dy = 0; dy <= 1; dy++)
    for (int dx = 0; dx <= 1; dx++) {
      g.setCursor(x2 + dx, y + dy);
      g.print("POSITIVA");
    }
  return x2 + g.textWidth("POSITIVA") + 1;
}

static void barraTopo(M5Canvas& g, const char* t) {
  int w = g.width();
  g.fillRect(0, 0, w, 30, C_LOGO_BG);                 // barra mais alta
  g.fillRect(0, 29, w, 1, C_LOGO_RED);                // linha fina vermelha embaixo
  int fimLogo = desenhaLogo(g);
  g.setFont(&fonts::Font2);
  char b[24];
  snprintf(b, sizeof b, "%s  %d/%d", t, pagina + 1, N_PAG);
  if (w - g.textWidth(b) - 8 < fimLogo + 8) snprintf(b, sizeof b, "%d/%d", pagina + 1, N_PAG);
  g.setTextColor(C_LOGO_GRAY, C_LOGO_BG);
  g.setCursor(w - g.textWidth(b) - 8, 8);
  g.print(b);
}

// Rodape: o que cada botao faz NESTA tela
static void barraBase(M5Canvas& g) {
  int w = g.width(), h = g.height();
  uint16_t cf = RGB565(241, 243, 247);
  g.fillRect(0, h - 22, w, 22, cf);
  g.fillRect(0, h - 22, w, 1, C_SEP);
  g.setFont(&fonts::Font2);
  g.setTextColor(C_TXT, cf);
  char a[20];
  snprintf(a, sizeof a, "A > %s", NOME_PAG[(pagina + 1) % N_PAG]);
  const char* bb = pagina == 4 ? "B  + 0,5s" : "B Testar";
  const char* cc = pagina == 4 ? "C  - 0,5s" : "C Parar";
  g.setCursor(6, h - 18);
  g.print(a);
  g.setCursor((w - g.textWidth(bb)) / 2 + 10, h - 18);
  g.print(bb);
  g.setCursor(w - g.textWidth(cc) - 6, h - 18);
  g.print(cc);
}

static void telaPainel(M5Canvas& g) {
  int w = g.width();
  barraTopo(g, "PAINEL");
  Status s = statusAtual();
  uint16_t cf = s.nivel == 0 ? C_OKBG : s.nivel == 1 ? C_WORKBG : C_ERRBG;
  uint16_t cb = s.nivel == 0 ? C_OK : s.nivel == 1 ? C_WORK : C_ERR;
  // Card de status
  g.fillRoundRect(6, 36, w - 12, 48, 8, cf);
  g.fillRect(6, 36, 4, 48, cb);
  g.setFont(&fonts::Font4);
  if (g.textWidth(s.titulo) > w - 28) g.setFont(&fonts::Font2);
  g.setTextColor(cb, cf);
  g.setCursor(16, 40);
  g.print(s.titulo);
  g.setFont(&fonts::Font2);
  g.setTextColor(C_TXT, cf);
  g.setCursor(16, 64);
  g.print(s.detalhe);
  // Card do ultimo codigo
  g.fillRoundRect(6, 90, w - 12, 24, 6, C_CARD);
  g.setFont(&fonts::Font2);
  char b[48];
  if (ultCodigo[0]) {
    txt(g, 10, 94, "Ultimo:", C_DIM, 7);
    snprintf(b, sizeof b, "%.14s %.5s", ultCodigo, ultHora);
    txt(g, 60, 94, b, C_TXT, 20);
  } else {
    txt(g, 10, 94, "Nenhum codigo lido ainda", C_DIM, 30);
  }
  // 3 metricas
  int bw = (w - 16) / 3;
  const char* lb[3] = {"CODIGOS", "LIGADA", "PARADA"};
  uint16_t cores[3] = {C_TXT, C_OK, C_ERR};
  char vals[3][16];
  snprintf(vals[0], 16, "%d", resHoje.codigos);
  snprintf(vals[1], 16, "%s", durCurta(resHoje.ligada).c_str());
  snprintf(vals[2], 16, "%s", durCurta(resHoje.parada).c_str());
  for (int i = 0; i < 3; i++) {
    int x = 6 + i * (bw + 2);
    g.fillRoundRect(x, 118, bw, 24, 6, C_CARD);
    g.setFont(&fonts::Font0);
    g.setTextColor(C_DIM, C_CARD);
    g.setCursor(x + 6, 118);
    g.print(lb[i]);
    g.setFont(&fonts::Font2);
    g.setTextColor(cores[i], C_CARD);
    g.setCursor(x + 6, 128);
    g.print(vals[i]);
  }
}

static void telaReles(M5Canvas& g) {
  int w = g.width();
  barraTopo(g, "RELES");
  int bw = (w - 12) / 4;
  for (int i = 0; i < 4; i++) {
    int x = 6 + i * (bw + 2);
    uint16_t fundo = relEst[i] ? C_OK : C_CARD;
    g.fillRoundRect(x, 36, bw, 56, 8, fundo);
    g.setFont(&fonts::Font4);
    g.setTextColor(relEst[i] ? 0xFFFF : C_TXT, fundo);
    g.setCursor(x + 10, 40);
    g.printf("R%d", i + 1);
    g.setFont(&fonts::Font2);
    g.setTextColor(relEst[i] ? 0xFFFF : C_DIM, fundo);
    g.setCursor(x + 6, 70);
    g.print(relEst[i] ? "FECHADO" : "aberto");
  }
  // card da tensao
  g.fillRoundRect(6, 100, w - 12, 42, 8, C_CARD);
  g.setFont(&fonts::Font0);
  txt(g, 12, 104, "ALIMENTACAO", C_DIM, 15);
  g.setFont(&fonts::Font4);
  char b[32];
  snprintf(b, sizeof b, "%.1f V", fonteV);
  g.setTextColor(fonteV < 6.0f ? C_ERR : C_OK, C_CARD);
  g.setCursor(14, 118);
  g.print(b);
  g.setFont(&fonts::Font0);
  if (fonteV < 6.0f) {
    txt(g, 100, 114, "Precisa 6-36V", C_ERR, 18);
    txt(g, 100, 128, "para os reles ligarem", C_ERR, 24);
  } else {
    snprintf(b, sizeof b, "Rele R%d usado", RELE_CANAL + 1);
    txt(g, 100, 114, b, C_DIM, 18);
    txt(g, 100, 128, "pela automacao", C_DIM, 20);
  }
}

// Uma linha compacta: bolinha + nome + valor. nivel: 0 ok, 1 aviso, 2 erro
static void linhaStatus(M5Canvas& g, int i, int nivel, const char* nome, const char* valor) {
  int y = 33 + i * 15;
  int w = g.width();
  uint16_t cor = nivel == 0 ? C_OK : nivel == 1 ? C_WORK : C_ERR;
  g.fillRoundRect(6, y, w - 12, 15, 3, C_CARD);
  g.fillCircle(14, y + 7, 4, cor);
  g.setFont(&fonts::Font2);
  txt(g, 24, y - 1, nome, C_DIM, 11);
  txt(g, 100, y - 1, valor, nivel == 2 ? C_ERR : C_TXT, 20);
}

static void telaRede(M5Canvas& g) {
  barraTopo(g, "ESTADOS");
  // Linha 0: cabo de rede
  linhaStatus(g, 0, linkOk ? 0 : 2, "Cabo rede", linkOk ? "conectado" : "DESCONECT.");
  // Linha 1: StamPLC IP
  linhaStatus(g, 1, linkOk ? 0 : 2, "IP local", ipTxt);
  // Linha 2: impressora
  String pi = ipImpressora.toString();
  linhaStatus(g, 2, online ? 0 : 2, "Impressora", online ? pi.c_str() : "sem resposta");
  // Linha 3: esteira (motor)
  linhaStatus(g, 3, esteiraLigada ? 0 : 1, "Esteira", esteiraLigada ? "RODANDO" : "parada");
  // Linha 4: alarme da impressora
  bool alarme = online && ultimoSnap.alarme;
  linhaStatus(g, 4, alarme ? 2 : 0, "Alarme imp.", alarme ? "ATIVO" : "normal");
  // Linha 5: alimentacao
  char v[16];
  snprintf(v, sizeof v, "%.1f V", fonteV);
  linhaStatus(g, 5, fonteV < 6.0f ? 2 : 0, "Alimentacao", v);
  // Linha 6: cartao SD
  linhaStatus(g, 6, sdOk ? 0 : 2, "Cartao SD", sdOk ? "gravando" : "NAO ACHADO");
  // Linha 7: hora
  char d[16], hh[10], b[24];
  carimbo(d, hh);
  snprintf(b, sizeof b, "%.5s %.5s", hh, d + 5);
  linhaStatus(g, 7, horaOk ? 0 : 1, "Hora", horaOk ? b : "nao acertada");
}

static void telaEventos(M5Canvas& g) {
  barraTopo(g, "EVENTOS");
  if (!telaCount) { txt(g, 10, 68, "Nenhum evento ainda", C_DIM); return; }
  for (int i = 0; i < telaCount && i < 5; i++) {
    const char* l = telaBuf[(telaPos - 1 - i + TELA_N * 2) % TELA_N];
    int y = 36 + i * 22;
    g.fillRoundRect(6, y, g.width() - 12, 20, 4, i == 0 ? C_WORKBG : C_CARD);
    g.setFont(&fonts::Font2);
    txt(g, 12, y + 2, l, i == 0 ? C_WORK : C_TXT, 33);
  }
}

static void telaAjustes(M5Canvas& g) {
  int w = g.width();
  barraTopo(g, "AJUSTES");
  g.fillRoundRect(6, 36, w - 12, 54, 8, C_CARD);
  g.setFont(&fonts::Font0);
  txt(g, 12, 40, "TEMPO DA ESTEIRA RODANDO", C_DIM, 28);
  char b[24];
  snprintf(b, sizeof b, "%u s", (unsigned)esteiraSegMs / 1000);
  g.setFont(&fonts::Font6);
  g.setTextColor(C_TXT, C_CARD);
  g.setCursor(14, 50);
  g.print(b);
  if (millis() - tSalvo < 2000) {
    g.fillRoundRect(w - 68, 44, 60, 20, 4, C_OKBG);
    g.setFont(&fonts::Font2);
    txt(g, w - 60, 46, "SALVO", C_OK, 6);
  } else {
    g.setFont(&fonts::Font0);
    txt(g, w - 108, 68, "B = +1s   C = -1s", C_DIM, 22);
  }
  g.fillRoundRect(6, 96, w - 12, 46, 8, C_CARD);
  g.setFont(&fonts::Font0);
  txt(g, 12, 100, "ORIGEM DA ESTEIRA", C_DIM, 24);
  g.setFont(&fonts::Font2);
  txt(g, 12, 115, esteiraDesc().c_str(), C_TXT, 32);
}

// Tela de operacao: fundo verde + tempo gigante branco. Aparece sozinha enquanto a esteira roda.
static void telaOperando(M5Canvas& g) {
  int w = g.width(), h = g.height();
  g.fillScreen(C_OK);
  // Topo: titulo
  g.setFont(&fonts::Font2);
  g.setTextColor(0xFFFF);
  const char* tit = "ESTEIRA RODANDO";
  g.setCursor((w - g.textWidth(tit)) / 2, 6);
  g.print(tit);
  // Tempo gigante + "s" ao lado, tudo centralizado em uma unica linha
  long resta = ((long)esteiraSegMs - (long)(millis() - tFase) + 999) / 1000;
  if (resta < 0) resta = 0;
  char tm[16];
  snprintf(tm, sizeof tm, "%ld", resta);
  g.setFont(&fonts::Font8);  // numero gigante
  int tw = g.textWidth(tm);
  int th = g.fontHeight();
  g.setFont(&fonts::Font4);  // "s" medio
  int sw = g.textWidth(" s");
  int total = tw + sw;
  int x0 = (w - total) / 2;
  int y0 = (h - th) / 2 + 2;  // centraliza vertical com um leve descer
  g.setFont(&fonts::Font8);
  g.setCursor(x0, y0);
  g.print(tm);
  g.setFont(&fonts::Font4);
  g.setCursor(x0 + tw + 2, y0 + th - 40);  // "s" alinhado ao pe do numero
  g.print("s");
  // Rodape: codigo
  if (ultCodigo[0]) {
    g.setFont(&fonts::Font2);
    char b[48];
    snprintf(b, sizeof b, "Codigo: %.16s", ultCodigo);
    g.setCursor((w - g.textWidth(b)) / 2, h - 20);
    g.print(b);
  }
}

// Tela gigante colorida para fases de operacao: fundo cor + titulo grande + subtitulo.
// Se mostraCodigo = true, escreve o codigo embaixo. corTxt = cor do titulo (contraste).
static void telaFase(M5Canvas& g, uint16_t fundo, uint16_t corTxt, const char* titulo,
                     const char* subt, bool mostraCodigo) {
  int w = g.width(), h = g.height();
  g.fillScreen(fundo);
  g.setFont(&fonts::Font4);
  g.setTextColor(corTxt);
  int tw = g.textWidth(titulo);
  // se o titulo nao couber, usa Font2
  if (tw > w - 16) { g.setFont(&fonts::Font2); tw = g.textWidth(titulo); }
  g.setCursor((w - tw) / 2, h / 2 - 24);
  g.print(titulo);
  g.setFont(&fonts::Font2);
  int sw = g.textWidth(subt);
  g.setCursor((w - sw) / 2, h / 2 + 6);
  g.print(subt);
  if (mostraCodigo && ultCodigo[0]) {
    char b[48];
    snprintf(b, sizeof b, "Codigo: %.16s", ultCodigo);
    g.setCursor((w - g.textWidth(b)) / 2, h - 20);
    g.print(b);
  }
}

static void desenhaTela() {
  auto& d = M5StamPLC.Display();
  if (!cv) {
    cv = new M5Canvas(&d);
    cv->setColorDepth(16);
    cv->setPsram(true);
    if (!cv->createSprite(d.width(), d.height())) {
      delete cv;
      cv = nullptr;
      return;
    }
  }
  // Em operacao: ignora navegacao e mostra a tela de ciclo colorida (fullscreen)
  if (fase == RODANDO) { telaOperando(*cv); cv->pushSprite(0, 0); return; }
  if (fase == PRE_PARA_PULSA || fase == PRE_PARA_ESPERA) {
    telaFase(*cv, C_WORK, 0xFFFF, "PARANDO ANTES", "Esteira ja rodava, ajustando", true);
    cv->pushSprite(0, 0);
    return;
  }
  if (fase == PULSA_LIGA || fase == AGUARDA_LIGA) {
    telaFase(*cv, C_WORK, 0xFFFF, "LIGANDO ESTEIRA", "Pulsando o botao...", true);
    cv->pushSprite(0, 0);
    return;
  }
  if (fase == PULSA_PARA || fase == AGUARDA_PARA) {
    telaFase(*cv, C_WORK, 0xFFFF, "PARANDO ESTEIRA", "Tempo esgotado", true);
    cv->pushSprite(0, 0);
    return;
  }
  if (fase == LIMPANDO) {
    uint16_t cf = falhaLimpeza ? C_ERR : RGB565(30, 120, 200);  // azul normal, vermelho se falha
    const char* sub = falhaLimpeza ? "Impressora nao responde, insistindo" : "Mandando limpar o cache...";
    telaFase(*cv, cf, 0xFFFF, falhaLimpeza ? "FALHA AO APAGAR" : "APAGANDO CODIGO", sub, true);
    cv->pushSprite(0, 0);
    return;
  }
  if (fase == ERRO) {
    telaFase(*cv, C_ERR, 0xFFFF, "ERRO NO CICLO", erroMsg, true);
    cv->pushSprite(0, 0);
    return;
  }
  cv->fillScreen(C_BG);
  switch (pagina) {
    case 0: telaPainel(*cv); break;
    case 1: telaReles(*cv); break;
    case 2: telaRede(*cv); break;
    case 3: telaEventos(*cv); break;
    default: telaAjustes(*cv); break;
  }
  barraBase(*cv);
  cv->pushSprite(0, 0);
}
