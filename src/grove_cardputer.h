#pragma once
// Plano B offline: Cardputer conectado via Grove UART direto no PLC.
// Protocolo texto, uma linha de comando por vez, resposta uma linha.
//
// Comandos aceitos (case-insensitive, terminados por \n):
//   ?           ou HELP      - lista comandos
//   STATE                    - fase + modo + rele + esteira + codigo + tempo
//   LIVE                     - mesmo JSON do /live.json (uma linha so)
//   LOG                      - ultimas 10 linhas do log
//   ENCODER                  - valor atual do contador
//   TEMPO <seg>              - muda tempo padrao da esteira
//   MODO AUTO | MANUAL       - troca modo
//   TESTAR                   - dispara ciclo manual
//   PARAR                    - aborta ciclo
//   AP ON | OFF              - liga/desliga o AP WiFi
//   REBOOT                   - reinicia o PLC
//
// A fiacao Grove do StamPLC usa GPIO 13 (RX) e GPIO 15 (TX) por padrao -
// ajuste em config.h se seu modulo tiver outra pinagem.

#ifndef GROVE_SERIAL_HABILITADO
  #define GROVE_SERIAL_HABILITADO  1
#endif
#ifndef GROVE_SERIAL_PIN_RX
  #define GROVE_SERIAL_PIN_RX  13
#endif
#ifndef GROVE_SERIAL_PIN_TX
  #define GROVE_SERIAL_PIN_TX  15
#endif
#ifndef GROVE_SERIAL_BAUD
  #define GROVE_SERIAL_BAUD    115200
#endif

#include <HardwareSerial.h>

// Este header e sempre incluido DEPOIS de manutencao.h no main.cpp,
// entao manutencao{LigaAP,DesligaAP,APAtivo} ja estao visiveis como static.
// manutencaoColetaStatus/manutencaoDespejaLogSerial sao definidas em main.cpp
// com linkagem externa (nao-static) - declaradas aqui:
int manutencaoColetaStatus(char* buf, size_t n);
void manutencaoDespejaLogSerial(Stream& s);

// Hooks do ciclo que o parser pode acionar (definidos em main.cpp):
void grovePararCiclo();
void groveDispararTeste();
bool groveMudaTempo(uint32_t ms);
void groveSetModo(bool manual);
uint32_t groveEncoder();
const char* groveFaseNome();

static HardwareSerial& groveSer = Serial1;
static String groveBuf = "";

static void groveBegin() {
#if GROVE_SERIAL_HABILITADO
  groveSer.begin(GROVE_SERIAL_BAUD, SERIAL_8N1, GROVE_SERIAL_PIN_RX, GROVE_SERIAL_PIN_TX);
  groveSer.println("# StamPLC Grove pronto - digite ? para ajuda");
#endif
}

static void groveImprimeAjuda() {
  groveSer.println(
    "? HELP STATE LIVE LOG ENCODER\n"
    "TEMPO <seg>  MODO AUTO|MANUAL\n"
    "TESTAR  PARAR  AP ON|OFF  REBOOT"
  );
}

static void groveProcessaLinha(String linha) {
  linha.trim();
  if (linha.length() == 0) return;
  String up = linha; up.toUpperCase();

  if (up == "?" || up == "HELP") { groveImprimeAjuda(); return; }

  if (up == "STATE") {
    groveSer.print("fase=");  groveSer.print(groveFaseNome());
    groveSer.print(" encoder="); groveSer.println(groveEncoder());
    return;
  }
  if (up == "LIVE") {
    char buf[600];
    int n = manutencaoColetaStatus(buf, sizeof buf);
    if (n > 0) groveSer.write((const uint8_t*)buf, n);
    groveSer.println();
    return;
  }
  if (up == "LOG") { manutencaoDespejaLogSerial(groveSer); return; }
  if (up == "ENCODER") { groveSer.println(groveEncoder()); return; }

  if (up.startsWith("TEMPO ")) {
    long seg = linha.substring(6).toInt();
    if (seg >= 60 && seg <= 36000) {
      groveMudaTempo((uint32_t)seg * 1000UL);
      groveSer.print("OK tempo="); groveSer.println(seg);
    } else {
      groveSer.println("ERR tempo invalido (60..36000 s)");
    }
    return;
  }
  if (up == "MODO AUTO")   { groveSetModo(false); groveSer.println("OK modo=auto"); return; }
  if (up == "MODO MANUAL") { groveSetModo(true);  groveSer.println("OK modo=manual"); return; }
  if (up == "TESTAR") { groveDispararTeste(); groveSer.println("OK disparado"); return; }
  if (up == "PARAR")  { grovePararCiclo();    groveSer.println("OK parado");    return; }
  if (up == "AP ON")  { manutencaoLigaAP();   groveSer.println("OK ap=on");     return; }
  if (up == "AP OFF") { manutencaoDesligaAP();groveSer.println("OK ap=off");    return; }
  if (up == "REBOOT") { groveSer.println("OK reboot..."); delay(200); ESP.restart(); return; }

  groveSer.print("ERR comando desconhecido: ");
  groveSer.println(linha);
}

static void groveLoop() {
#if GROVE_SERIAL_HABILITADO
  while (groveSer.available()) {
    char ch = groveSer.read();
    if (ch == '\r') continue;
    if (ch == '\n') { groveProcessaLinha(groveBuf); groveBuf = ""; continue; }
    if (groveBuf.length() < 120) groveBuf += ch;
  }
#endif
}
