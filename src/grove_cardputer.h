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
//   MB READ <fc> <addr> <n>  - le Modbus (fc 1..4). Ex: MB READ 3 200 25
//   MB WRITE <fc> <addr> <v> - escreve Modbus (fc 5 ou 6). Ex: MB WRITE 5 2 1
//   SET <chave> <valor>      - atalho pros /set em runtime (tempo/mixa/mixb/...)
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
bool mbManutLeRegs(uint8_t fc, uint16_t addr, uint16_t qty, uint16_t* out);
bool mbManutLeBits(uint8_t fc, uint16_t addr, uint16_t qty, bool* out);
bool mbManutEscreveBit(uint16_t addr, bool on);
bool mbManutEscreveReg(uint16_t addr, uint16_t v);
bool manutAjustaTempo(uint32_t ms);
bool manutAjustaMixA(uint32_t ms);
bool manutAjustaMixB(uint32_t ms);
bool manutAjustaEspera(uint32_t ms);
void manutAjustaModo(bool manual);
void manutAjustaSireneHab(bool on);
void manutAjustaEncF(bool on);

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
    "TESTAR  PARAR  AP ON|OFF  REBOOT\n"
    "MB READ <fc> <addr> <n>  (fc 1..4)\n"
    "MB WRITE <fc> <addr> <v> (fc 5 ou 6)\n"
    "SET <chave> <valor>  (tempo mixa mixb espera modo sirene encf)"
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

  // MB READ <fc> <addr> <n>
  if (up.startsWith("MB READ ")) {
    int p1 = 8, p2 = linha.indexOf(' ', p1 + 1), p3 = linha.indexOf(' ', p2 + 1);
    if (p2 < 0 || p3 < 0) { groveSer.println("ERR uso: MB READ <fc> <addr> <n>"); return; }
    int fc = linha.substring(p1, p2).toInt();
    int addr = linha.substring(p2 + 1, p3).toInt();
    int n = linha.substring(p3 + 1).toInt();
    if (fc == 3 || fc == 4) {
      if (n < 1 || n > 32) { groveSer.println("ERR n fora de 1..32"); return; }
      uint16_t vals[32];
      if (!mbManutLeRegs(fc, addr, n, vals)) { groveSer.println("ERR timeout Modbus"); return; }
      groveSer.print("OK regs=");
      for (int i = 0; i < n; i++) { if (i) groveSer.print(","); groveSer.print(vals[i]); }
      groveSer.print(" ascii='");
      for (int i = 0; i < n; i++) {
        char hi = (vals[i] >> 8) & 0xFF, lo = vals[i] & 0xFF;
        groveSer.write((hi >= 32 && hi < 127) ? hi : '.');
        groveSer.write((lo >= 32 && lo < 127) ? lo : '.');
      }
      groveSer.println("'");
      return;
    }
    if (fc == 1 || fc == 2) {
      if (n < 1 || n > 64) { groveSer.println("ERR n fora de 1..64"); return; }
      bool bits[64];
      if (!mbManutLeBits(fc, addr, n, bits)) { groveSer.println("ERR timeout Modbus"); return; }
      groveSer.print("OK bits=");
      for (int i = 0; i < n; i++) groveSer.print(bits[i] ? "1" : "0");
      groveSer.println();
      return;
    }
    groveSer.println("ERR fc precisa ser 1,2,3 ou 4");
    return;
  }
  // MB WRITE <fc> <addr> <v>
  if (up.startsWith("MB WRITE ")) {
    int p1 = 9, p2 = linha.indexOf(' ', p1 + 1), p3 = linha.indexOf(' ', p2 + 1);
    if (p2 < 0 || p3 < 0) { groveSer.println("ERR uso: MB WRITE <fc> <addr> <v>"); return; }
    int fc = linha.substring(p1, p2).toInt();
    int addr = linha.substring(p2 + 1, p3).toInt();
    long v = linha.substring(p3 + 1).toInt();
    bool ok;
    if (fc == 5) ok = mbManutEscreveBit(addr, v != 0);
    else if (fc == 6) ok = mbManutEscreveReg(addr, (uint16_t)v);
    else { groveSer.println("ERR fc precisa ser 5 ou 6"); return; }
    groveSer.println(ok ? "OK escrito" : "ERR escrita falhou");
    return;
  }
  // SET <chave> <valor>
  if (up.startsWith("SET ")) {
    int p1 = 4, p2 = linha.indexOf(' ', p1 + 1);
    if (p2 < 0) { groveSer.println("ERR uso: SET <chave> <valor>"); return; }
    String k = linha.substring(p1, p2); k.toLowerCase();
    String v = linha.substring(p2 + 1); v.trim();
    bool ok = true;
    if (k == "tempo")  ok = manutAjustaTempo((uint32_t)v.toInt() * 1000UL);
    else if (k == "mixa")   ok = manutAjustaMixA((uint32_t)v.toInt() * 1000UL);
    else if (k == "mixb")   ok = manutAjustaMixB((uint32_t)v.toInt() * 1000UL);
    else if (k == "espera") ok = manutAjustaEspera((uint32_t)v.toInt() * 1000UL);
    else if (k == "modo")   manutAjustaModo(v == "manual");
    else if (k == "sirene") manutAjustaSireneHab(v == "1");
    else if (k == "encf")   manutAjustaEncF(v == "1");
    else { groveSer.println("ERR chave desconhecida"); return; }
    groveSer.println(ok ? "OK" : "ERR valor invalido");
    return;
  }

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
