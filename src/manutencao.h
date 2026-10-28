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
int manutencaoColetaStatus(char* buf, size_t n);
void manutencaoDespejaLog(Client& c);

// Ajustes rapidos (nao reiniciam o PLC)
bool manutAjustaTempo(uint32_t ms);
bool manutAjustaMixA(uint32_t ms);
bool manutAjustaMixB(uint32_t ms);
bool manutAjustaEspera(uint32_t ms);
void manutAjustaModo(bool manual);
void manutAjustaSireneHab(bool on);
bool manutAjustaSirHorarios(const char* s);
void manutAjustaEncF(bool on);
void manutTestar();
void manutParar();

// Modbus explorer (reusa cliente persistente)
bool mbManutLeRegs(uint8_t fc, uint16_t addr, uint16_t qty, uint16_t* out);
bool mbManutLeBits(uint8_t fc, uint16_t addr, uint16_t qty, bool* out);
bool mbManutEscreveBit(uint16_t addr, bool on);
bool mbManutEscreveReg(uint16_t addr, uint16_t v);

// param() de paginas.h (ja incluido antes deste header)
String param(const String& q, const char* k);

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

inline bool manutencaoAPAtivo() { return apAtivo; }

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
    ".fld{display:flex;gap:6px;align-items:center;margin:4px 0;flex-wrap:wrap}"
    ".fld label{min-width:90px;color:#8aa;font-size:12px}"
    ".fld input,.fld select{background:#0a0b0e;color:#eee;border:1px solid #333;border-radius:5px;padding:5px 8px;font:13px ui-monospace,monospace;width:120px}"
    ".fld button{background:#4af;color:#000;border:0;border-radius:5px;padding:5px 10px;font-weight:600;cursor:pointer}"
    ".msg{color:#4a4;font-size:12px;margin-left:6px}.msg.err{color:#f44}"
    "</style></head><body>"
    "<h1>MANUTENCAO EM CAMPO</h1>"
    "<div id=live>carregando...</div>"
    "<p><a class=btn href='/'>Painel completo</a>"
    "<a class='btn sec' href='/update'>Atualizar firmware</a>"
    "<a class='btn red' href='/ap/off'>Desligar AP</a></p>"

    "<h1>AJUSTES RAPIDOS</h1>"
    "<div class=c>"
    "<div class=fld><label>Tempo padrao</label><input id=i_tempo type=number min=60 max=36000 step=10><span>s</span><button onclick=\"sv('tempo',i_tempo.value)\">Aplicar</button><span id=m_tempo class=msg></span></div>"
    "<div class=fld><label>MIX A</label><input id=i_mixa type=number min=60 max=36000 step=10><span>s</span><button onclick=\"sv('mixa',i_mixa.value)\">Aplicar</button><span id=m_mixa class=msg></span></div>"
    "<div class=fld><label>MIX B</label><input id=i_mixb type=number min=60 max=36000 step=10><span>s</span><button onclick=\"sv('mixb',i_mixb.value)\">Aplicar</button><span id=m_mixb class=msg></span></div>"
    "<div class=fld><label>Modo</label><select id=i_modo><option value=auto>AUTO</option><option value=manual>MANUAL</option></select><button onclick=\"sv('modo',i_modo.value)\">Aplicar</button><span id=m_modo class=msg></span></div>"
    "<div class=fld><label>Sirene</label><select id=i_sir><option value=0>desligada</option><option value=1>habilitada</option></select><button onclick=\"sv('sirene',i_sir.value)\">Aplicar</button><span id=m_sirene class=msg></span></div>"
    "<div class=fld><label>Horarios</label><input id=i_sirH style='width:200px' placeholder='07:00,16:00,00:00'><button onclick=\"sv('sirHor',i_sirH.value)\">Aplicar</button><span id=m_sirHor class=msg></span></div>"
    "<div class=fld><label>Encoder fim</label><select id=i_encF><option value=0>observando</option><option value=1>ativo</option></select><button onclick=\"sv('encF',i_encF.value)\">Aplicar</button><span id=m_encF class=msg></span></div>"
    "<div class=fld><label>Acao</label><button onclick=\"sv('acao','testar')\">TESTAR ciclo</button><button style='background:#f44;color:#fff' onclick=\"sv('acao','parar')\">PARAR</button><span id=m_acao class=msg></span></div>"
    "</div>"

    "<h1>MODBUS EXPLORER</h1>"
    "<div class=c>"
    "<div class=fld><label>FC</label><select id=mb_fc>"
    "<option value=3>3 - holding read</option>"
    "<option value=4>4 - input read</option>"
    "<option value=1>1 - coils read</option>"
    "<option value=2>2 - discrete read</option>"
    "<option value=6>6 - holding write</option>"
    "<option value=5>5 - coil write</option></select></div>"
    "<div class=fld><label>Endereco</label><input id=mb_a type=number min=0 max=65535 value=200><label style='min-width:40px'>N/V</label><input id=mb_n type=number min=1 max=256 value=1><button onclick=mbgo()>Ler/Escrever</button></div>"
    "<pre id=mb_out style='background:#0a0b0e;padding:10px;border-radius:6px;margin:4px 0;font-size:12px;max-height:200px;overflow:auto;white-space:pre-wrap'>-</pre>"
    "<div style='font-size:11px;color:#8aa;line-height:1.6'>Enderecos uteis:"
    " 4x0201=200 (texto scanner)"
    " 3x0002=1 (dados em cache)"
    " 4x0061=60 (encoder)"
    " 1x0018=23 (tampa aberta)"
    " 0x0002=1 (clear cache)"
    "</div></div>"

    "<h1>LOG</h1><pre id=log>carregando...</pre>"

    "<script>"
    "let primeiro=true;"
    "async function sv(k,v){"
    " try{"
    "  const r=await fetch('/set?'+k+'='+encodeURIComponent(v));"
    "  const j=await r.json();"
    "  const el=document.getElementById('m_'+k);"
    "  if(el){el.textContent=j.msg;el.className='msg'+(j.ok?'':' err');setTimeout(()=>el.textContent='',3000);}"
    " }catch(e){alert(e);}"
    "}"
    "async function mbgo(){"
    " const fc=mb_fc.value,a=mb_a.value,n=mb_n.value;"
    " const url=(fc==5||fc==6)?`/mb?fc=${fc}&a=${a}&v=${n}`:`/mb?fc=${fc}&a=${a}&n=${n}`;"
    " mb_out.textContent='pedindo...';"
    " try{const r=await fetch(url);const j=await r.json();mb_out.textContent=JSON.stringify(j,null,2);}"
    " catch(e){mb_out.textContent='erro: '+e;}"
    "}"
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
    "  if(primeiro){"
    "   i_tempo.value=j.tempo_alvo_s; i_mixa.value=j.tempo_mixa_s; i_mixb.value=j.tempo_mixb_s;"
    "   i_modo.value=j.modo; i_sir.value=j.sirene_hab; i_encF.value=j.encoder_fim_hab;"
    "   primeiro=false;"
    "  }"
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

static void respondeJson(Client& c, const char* json) {
  c.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
          "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
  c.print(json);
}

// GET /set?k=v - aplica em runtime (sem reboot). Devolve JSON {ok,msg}.
// Chaves: tempo, mixa, mixb, espera (em segundos); modo=auto|manual;
//         sirene=0|1; sirHor=HH:MM,HH:MM,...; encF=0|1; acao=testar|parar
static void rotaSet(Client& c, const String& q) {
  char resp[160];
  bool ok = true;
  const char* msg = "sem parametros";
  String s;
  if ((s = param(q, "tempo")).length())  { long v = s.toInt(); ok = manutAjustaTempo(v * 1000UL); msg = ok ? "tempo ok" : "fora de 60..36000 s"; }
  else if ((s = param(q, "mixa")).length())  { long v = s.toInt(); ok = manutAjustaMixA(v * 1000UL);  msg = ok ? "mixa ok"  : "fora de 60..36000 s"; }
  else if ((s = param(q, "mixb")).length())  { long v = s.toInt(); ok = manutAjustaMixB(v * 1000UL);  msg = ok ? "mixb ok"  : "fora de 60..36000 s"; }
  else if ((s = param(q, "espera")).length()){ long v = s.toInt(); ok = manutAjustaEspera(v * 1000UL);msg = ok ? "espera ok": "fora de 10..3600 s"; }
  else if ((s = param(q, "modo")).length())  { manutAjustaModo(s == "manual"); msg = "modo ok"; }
  else if ((s = param(q, "sirene")).length()){ manutAjustaSireneHab(s == "1"); msg = "sirene ok"; }
  else if ((s = param(q, "sirHor")).length()){ ok = manutAjustaSirHorarios(s.c_str()); msg = ok ? "horarios ok" : "formato HH:MM,HH:MM"; }
  else if ((s = param(q, "encF")).length())  { manutAjustaEncF(s == "1"); msg = "encF ok"; }
  else if ((s = param(q, "acao")).length())  {
    if (s == "testar") { manutTestar(); msg = "ciclo disparado"; }
    else if (s == "parar") { manutParar(); msg = "ciclo abortado"; }
    else { ok = false; msg = "acao=testar|parar"; }
  }
  else ok = false;
  snprintf(resp, sizeof resp, "{\"ok\":%s,\"msg\":\"%s\"}", ok ? "true" : "false", msg);
  respondeJson(c, resp);
}

// GET /mb?fc=3&a=200&n=1       le holding/input (fc 3/4) ou coils/discretes (fc 1/2)
// GET /mb?fc=6&a=201&v=0       escreve 1 holding register
// GET /mb?fc=5&a=2&v=1         escreve 1 coil
static void rotaModbus(Client& c, const String& q) {
  int fc = param(q, "fc").toInt();
  uint16_t addr = (uint16_t) param(q, "a").toInt();
  String sv = param(q, "v");
  int n = param(q, "n").length() ? param(q, "n").toInt() : 1;

  char out[1024];
  int w = 0;

  if (fc == 3 || fc == 4) {
    if (n < 1 || n > 32) { respondeJson(c, "{\"ok\":false,\"msg\":\"n fora de 1..32 (limite do buffer)\"}"); return; }
    uint16_t vals[32];
    if (!mbManutLeRegs(fc, addr, n, vals)) { respondeJson(c, "{\"ok\":false,\"msg\":\"timeout ou erro Modbus\"}"); return; }
    w += snprintf(out + w, sizeof out - w, "{\"ok\":true,\"fc\":%d,\"addr\":%u,\"n\":%d,\"hex\":[", fc, addr, n);
    for (int i = 0; i < n; i++) w += snprintf(out + w, sizeof out - w, "%s\"0x%04X\"", i ? "," : "", vals[i]);
    w += snprintf(out + w, sizeof out - w, "],\"dec\":[");
    for (int i = 0; i < n; i++) w += snprintf(out + w, sizeof out - w, "%s%u", i ? "," : "", vals[i]);
    w += snprintf(out + w, sizeof out - w, "],\"ascii\":\"");
    for (int i = 0; i < n; i++) {
      char hi = (vals[i] >> 8) & 0xFF, lo = vals[i] & 0xFF;
      out[w++] = (hi >= 32 && hi < 127) ? hi : '.';
      out[w++] = (lo >= 32 && lo < 127) ? lo : '.';
    }
    w += snprintf(out + w, sizeof out - w, "\"}");
    respondeJson(c, out); return;
  }
  if (fc == 1 || fc == 2) {
    if (n < 1 || n > 256) { respondeJson(c, "{\"ok\":false,\"msg\":\"n fora de 1..256\"}"); return; }
    bool bits[256];
    if (!mbManutLeBits(fc, addr, n, bits)) { respondeJson(c, "{\"ok\":false,\"msg\":\"timeout ou erro Modbus\"}"); return; }
    w += snprintf(out + w, sizeof out - w, "{\"ok\":true,\"fc\":%d,\"addr\":%u,\"n\":%d,\"bits\":[", fc, addr, n);
    for (int i = 0; i < n; i++) w += snprintf(out + w, sizeof out - w, "%s%d", i ? "," : "", bits[i] ? 1 : 0);
    w += snprintf(out + w, sizeof out - w, "]}");
    respondeJson(c, out); return;
  }
  if (fc == 5) {
    bool on = sv == "1" || sv.toInt() != 0;
    bool ok = mbManutEscreveBit(addr, on);
    snprintf(out, sizeof out, "{\"ok\":%s,\"fc\":5,\"addr\":%u,\"v\":%d}", ok ? "true" : "false", addr, on ? 1 : 0);
    respondeJson(c, out); return;
  }
  if (fc == 6) {
    uint16_t v = (uint16_t) sv.toInt();
    bool ok = mbManutEscreveReg(addr, v);
    snprintf(out, sizeof out, "{\"ok\":%s,\"fc\":6,\"addr\":%u,\"v\":%u}", ok ? "true" : "false", addr, v);
    respondeJson(c, out); return;
  }
  respondeJson(c, "{\"ok\":false,\"msg\":\"fc precisa ser 1,2,3,4,5 ou 6\"}");
}

static bool manutencaoRota(Client& c, const String& metodo, const String& caminho,
                           const String& q, uint32_t contentLen) {
  if (caminho == "/manut")      { rotaManutHTML(c); return true; }
  if (caminho == "/live.json")  { rotaLiveJson(c); return true; }
  if (caminho == "/log.txt")    { rotaLogTxt(c); return true; }
  if (caminho == "/set")        { rotaSet(c, q); return true; }
  if (caminho == "/mb")         { rotaModbus(c, q); return true; }
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
