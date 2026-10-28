#pragma once
// Modo manutencao: AP WiFi + OTA + endpoints de inspecao em tempo real.
// NAO interfere na comunicacao Ethernet com a impressora (hardware separado:
// W5500 por SPI vs radio WiFi do ESP32-S3). Ambos rodam em paralelo.
//
// Como o Ramon usa isso em campo:
//   1. Segura botao A por 3s em qualquer pagina do PLC -> AP sobe
//   2. Cardputer/celular conecta no SSID mostrado na tela
//   3. Abre http://192.168.4.1/ -> ve TUDO que roda na impressora (ao vivo)
//   4. Para atualizar firmware: /update + envia .bin do Cardputer
//   5. Timeout automatico de 30 min caso esqueca ligado
//
// Rotas extras (so disponiveis quando AP esta no ar):
//   GET  /manut        - pagina HTML com dashboard ao vivo + link OTA
//   GET  /live.json    - snapshot JSON do estado (polling do Cardputer)
//   GET  /log.txt      - ultimas N linhas do log
//   GET  /update       - formulario para subir firmware .bin
//   POST /update       - recebe binario cru no body (Content-Length obrigatorio)
//   GET  /ap/off       - desliga o AP imediatamente

#include <WiFi.h>
#include <Update.h>

// ---- Configuracao do AP ----------------------------------------------
#ifndef MANUTENCAO_AP_SSID
  #define MANUTENCAO_AP_SSID    "StamPLC-Sumi"
#endif
#ifndef MANUTENCAO_AP_SENHA
  #define MANUTENCAO_AP_SENHA   "sumi1234"       // minimo 8 chars
#endif
#ifndef MANUTENCAO_AP_TIMEOUT_MS
  #define MANUTENCAO_AP_TIMEOUT_MS  1800000UL    // 30 min
#endif

// ---- Interface implementada pelo main.cpp ----------------------------
// Preenche snapshot JSON (nao mais que ~500 bytes). Retorna bytes escritos.
int manutencaoColetaStatus(char* buf, size_t n);
// Despeja log circular no cliente (texto puro, uma linha por entrada).
void manutencaoDespejaLog(Client& c);

// ---- Estado interno --------------------------------------------------
static WiFiServer webAP(80);
static bool apAtivo = false;
static unsigned long apUltimoCliente = 0;

// ---- API publica -----------------------------------------------------
static void manutencaoBegin() { apAtivo = false; }

static void manutencaoLigaAP() {
  if (apAtivo) return;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(MANUTENCAO_AP_SSID, MANUTENCAO_AP_SENHA, 6, 0, 2);
  IPAddress apIp(192, 168, 4, 1), apMask(255, 255, 255, 0);
  WiFi.softAPConfig(apIp, apIp, apMask);
  webAP.begin();
  apAtivo = true;
  apUltimoCliente = millis();
}

static void manutencaoDesligaAP() {
  if (!apAtivo) return;
  webAP.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  apAtivo = false;
}

static bool manutencaoAPAtivo() { return apAtivo; }

static void manutencaoLoop() {
  if (!apAtivo) return;
  if (millis() - apUltimoCliente > MANUTENCAO_AP_TIMEOUT_MS) manutencaoDesligaAP();
}

static void manutencaoToggleAP() {
  if (apAtivo) manutencaoDesligaAP(); else manutencaoLigaAP();
}

// Forward (definido em paginas.h, incluido antes deste no main.cpp)
void respondeHTTP(Client& c);

static void manutencaoAtendeAP() {
  if (!apAtivo) return;
  WiFiClient wc = webAP.available();
  if (wc) {
    apUltimoCliente = millis();
    respondeHTTP(wc);
  }
}

// ---- Rotas HTTP ------------------------------------------------------
static void rotaLiveJson(Client& c) {
  char buf[600];
  int n = manutencaoColetaStatus(buf, sizeof buf);
  if (n <= 0) { strcpy(buf, "{}"); n = 2; }
  c.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
          "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n");
  c.print("Content-Length: "); c.print(n); c.print("\r\n\r\n");
  c.write((const uint8_t*)buf, n);
}

static void rotaLogTxt(Client& c) {
  c.print("HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
          "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
  manutencaoDespejaLog(c);
}

static void rotaManutHTML(Client& c) {
  c.print("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
          "Cache-Control: no-store\r\nConnection: close\r\n\r\n");
  c.print(F(
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Manutencao StamPLC</title>"
    "<style>body{font:14px system-ui;margin:0;background:#111;color:#eee;padding:12px}"
    "h1{font-size:16px;margin:4px 0 10px;color:#4af}"
    ".k{color:#8aa;font-size:11px;text-transform:uppercase;letter-spacing:.4px}"
    ".v{font-size:17px;font-weight:600;margin-bottom:4px;font-family:ui-monospace,monospace}"
    ".c{background:#1c1f26;border-radius:8px;padding:10px 12px;margin-bottom:8px;border-left:3px solid #4af}"
    ".c.ok{border-color:#4a4}.c.wk{border-color:#fa0}.c.er{border-color:#f44}"
    ".row{display:flex;gap:8px;flex-wrap:wrap}.row>.c{flex:1 1 150px}"
    "a.btn{display:inline-block;background:#4af;color:#000;text-decoration:none;padding:8px 14px;border-radius:6px;font-weight:600;margin:4px 4px 4px 0}"
    "a.red{background:#f44;color:#fff}a.sec{background:#2a2d35;color:#eee}"
    "pre{background:#0a0b0e;padding:10px;border-radius:6px;max-height:50vh;overflow:auto;font-size:12px;margin:0;white-space:pre-wrap}"
    "</style></head><body>"
    "<h1>MANUTENCAO EM CAMPO</h1>"
    "<div id=live>carregando...</div>"
    "<p><a class=btn href='/'>Painel completo</a>"
    "<a class='btn sec' href='/log.txt'>Log cru</a>"
    "<a class='btn sec' href='/update'>Atualizar firmware</a>"
    "<a class='btn red' href='/ap/off'>Desligar AP</a></p>"
    "<h1>LOG</h1><pre id=log>carregando...</pre>"
    "<script>"
    "async function tick(){"
    " try{"
    "  const r=await fetch('/live.json'); const j=await r.json();"
    "  const cls=j.fase=='ERRO'?'er':(j.fase=='OCIOSO'?'ok':'wk');"
    "  document.getElementById('live').innerHTML="
    "   `<div class='c ${cls}'><div class=k>Fase</div><div class=v>${j.fase}</div></div>`+"
    "   `<div class=row>"
    "     <div class=c><div class=k>Codigo</div><div class=v>${j.codigo||'-'}</div></div>"
    "     <div class=c><div class=k>Modo</div><div class=v>${j.modo}</div></div>"
    "     <div class=c><div class=k>Tempo restante</div><div class=v>${j.tempo_restante_s}s</div></div>"
    "     <div class=c><div class=k>Rele</div><div class=v>${j.rele?'ON':'OFF'}</div></div>"
    "     <div class=c><div class=k>Esteira</div><div class=v>${j.esteira?'LIGADA':'parada'}</div></div>"
    "     <div class=c><div class=k>Encoder</div><div class=v>${j.encoder}</div></div>"
    "     <div class=c><div class=k>IP PLC</div><div class=v>${j.ip_plc}</div></div>"
    "     <div class=c><div class=k>IP impr.</div><div class=v>${j.ip_impressora}</div></div>"
    "   </div>`;"
    "  const l=await fetch('/log.txt'); document.getElementById('log').textContent=await l.text();"
    " }catch(e){document.getElementById('live').textContent='sem resposta: '+e;}"
    "}"
    "tick(); setInterval(tick, 1500);"
    "</script></body></html>"
  ));
}

static void rotaUpdateGET(Client& c) {
  c.print("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
          "Cache-Control: no-store\r\nConnection: close\r\n\r\n");
  c.print(F(
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Atualizar firmware</title>"
    "<style>body{font:14px system-ui;margin:0;background:#111;color:#eee;padding:14px}"
    "h1{color:#4af;font-size:18px;margin:4px 0 12px}"
    "input[type=file]{color:#eee;background:#1c1f26;padding:10px;border-radius:6px;width:100%;max-width:300px;margin:8px 0}"
    "button{background:#4af;color:#000;border:0;padding:10px 20px;border-radius:6px;font:600 15px system-ui;cursor:pointer}"
    "button:disabled{opacity:.5}"
    "#log{background:#0a0b0e;padding:10px;border-radius:6px;margin-top:12px;white-space:pre-wrap;font-family:ui-monospace,monospace;font-size:12px}"
    ".bar{background:#2a2d35;border-radius:6px;overflow:hidden;height:12px;margin:8px 0;max-width:300px}"
    ".bar>i{display:block;height:100%;background:#4af;width:0;transition:width .2s}"
    "a{color:#4af}</style></head><body>"
    "<h1>Atualizar firmware .bin</h1>"
    "<p>Selecione o <code>firmware.bin</code> gerado pelo PlatformIO.<br>"
    "Durante o upload o PLC fica OFFLINE por ~30s. Nao abra tampa/leia codigos neste momento.</p>"
    "<form id=f><input type=file id=arq accept='.bin' required>"
    "<div class=bar><i id=prog></i></div>"
    "<button id=b type=submit>Enviar</button></form>"
    "<div id=log></div><p><a href='/manut'>&larr; voltar</a></p>"
    "<script>"
    "const f=document.getElementById('f'),a=document.getElementById('arq'),"
    "b=document.getElementById('b'),prog=document.getElementById('prog'),"
    "log=document.getElementById('log');"
    "f.addEventListener('submit',async(e)=>{e.preventDefault();"
    " const file=a.files[0]; if(!file){return;}"
    " b.disabled=true; log.textContent='enviando '+(file.size/1024).toFixed(1)+' KB...';"
    " prog.style.width='5%';"
    " try{"
    "  const r=await fetch('/update',{method:'POST',body:file,"
    "    headers:{'Content-Type':'application/octet-stream'}});"
    "  const t=await r.text();"
    "  prog.style.width='100%';"
    "  log.textContent=t+(r.ok?'\\n\\nAguardando reboot...':'\\n\\nFALHOU');"
    "  if(r.ok){setTimeout(()=>location.href='/manut',15000);}"
    "  else{b.disabled=false;}"
    " }catch(e){log.textContent='erro de rede: '+e; b.disabled=false;}"
    "});"
    "</script></body></html>"
  ));
}

static void rotaUpdatePOST(Client& c, uint32_t contentLen) {
  if (contentLen == 0 || contentLen > 4UL * 1024UL * 1024UL) {
    c.print("HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
            "Content-Length ausente ou invalido\n");
    return;
  }
  if (!Update.begin(contentLen)) {
    c.print("HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
            "Update.begin falhou: ");
    c.print(Update.errorString());
    c.print("\n");
    return;
  }
  uint32_t lidos = 0;
  uint8_t buf[1024];
  unsigned long tUltimo = millis();
  while (lidos < contentLen && c.connected() && (millis() - tUltimo) < 15000UL) {
    int disp = c.available();
    if (disp <= 0) { delay(1); continue; }
    size_t falta = contentLen - lidos;
    size_t pedido = (size_t)disp < sizeof buf ? (size_t)disp : sizeof buf;
    if (pedido > falta) pedido = falta;
    int lido = c.read(buf, pedido);
    if (lido <= 0) continue;
    if (Update.write(buf, lido) != (size_t)lido) {
      c.print("HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
              "escrita falhou: ");
      c.print(Update.errorString());
      c.print("\n");
      Update.abort();
      return;
    }
    lidos += lido;
    tUltimo = millis();
  }
  if (lidos != contentLen) {
    c.print("HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
            "upload incompleto\n");
    Update.abort();
    return;
  }
  if (!Update.end(true)) {
    c.print("HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
            "Update.end falhou: ");
    c.print(Update.errorString());
    c.print("\n");
    return;
  }
  c.print("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
          "OK - firmware gravado. Reiniciando...\n");
  c.flush();
  delay(500);
  ESP.restart();
}

static bool manutencaoRota(Client& c, const String& metodo, const String& caminho,
                           const String& q, uint32_t contentLen) {
  (void)q;
  if (caminho == "/manut")      { rotaManutHTML(c); return true; }
  if (caminho == "/live.json")  { rotaLiveJson(c); return true; }
  if (caminho == "/log.txt")    { rotaLogTxt(c); return true; }
  if (caminho == "/update" && metodo == "GET")  { rotaUpdateGET(c); return true; }
  if (caminho == "/update" && metodo == "POST") { rotaUpdatePOST(c, contentLen); return true; }
  if (caminho == "/ap/off") {
    c.print("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
            "AP desligado\n");
    c.flush(); delay(200); manutencaoDesligaAP();
    return true;
  }
  return false;
}
