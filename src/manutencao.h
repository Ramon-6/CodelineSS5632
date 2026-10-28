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
int manutReleAciona(int canal, int op);  // op: 0=off 1=on 2=pulso

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
  // Desliga power save do radio: sem isso o AP cai periodicamente e o celular
  // perde conexao a cada 1-2 min. Com NONE o AP fica estavel.
  WiFi.setSleep(WIFI_PS_NONE);
  // max_connection = 4 (padrao do ESP32 ja e 4, mas deixo explicito)
  WiFi.softAP(MANUTENCAO_AP_SSID, MANUTENCAO_AP_SENHA, 6, 0, 4);
  IPAddress apIp(192, 168, 4, 1), apMask(255, 255, 255, 0);
  WiFi.softAPConfig(apIp, apIp, apMask);
  // Potencia maxima no radio pra garantir sinal em painel metalico
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
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
  // Enquanto houver pelo menos 1 celular/cardputer conectado no AP, reseta
  // o contador - timeout so corre quando ninguem esta usando.
  if (WiFi.softAPgetStationNum() > 0) apUltimoCliente = millis();
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
    "<title>Batoff Manutencao</title>"
    "<style>"
    "*{box-sizing:border-box}"
    "body{font:14px system-ui,-apple-system,sans-serif;margin:0;background:#0f0f0f;color:#d4d4d4}"
    ".wrap{max-width:900px;margin:0 auto;padding:0 12px 40px}"
    ".bar{background:#181818;border-bottom:2px solid #3ddc84;padding:10px 14px;margin:0 -12px 12px;display:flex;align-items:center;justify-content:space-between;position:sticky;top:0;z-index:5;flex-wrap:wrap;gap:8px}"
    ".bar h1{margin:0;font-size:15px;letter-spacing:2px;color:#3ddc84}"
    ".bar .acts{display:flex;gap:6px;flex-wrap:wrap}"
    "h2{color:#3ddc84;font-size:10px;letter-spacing:2px;margin:14px 0 6px;border-bottom:1px solid #303030;padding-bottom:4px}"
    ".card{background:#181818;border:1px solid #303030;border-radius:6px;padding:10px 12px;margin-bottom:6px}"
    ".card.hero{border-left:4px solid #3ddc84;padding:12px 14px}"
    ".card.hero.wk{border-left-color:#e0d204}.card.hero.er{border-left-color:#f44}"
    ".k{color:#888;font-size:10px;text-transform:uppercase;letter-spacing:.6px;margin-bottom:2px}"
    ".v{font-size:15px;font-weight:600;font-family:ui-monospace,SF Mono,monospace;color:#d4d4d4}"
    ".v.big{font-size:22px}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:6px}"
    ".btn{display:inline-flex;align-items:center;justify-content:center;background:#303030;color:#d4d4d4;border:1px solid #303030;border-radius:5px;padding:7px 12px;font:600 13px system-ui;text-decoration:none;cursor:pointer;transition:all .1s}"
    ".btn:hover{border-color:#3ddc84;color:#3ddc84}"
    ".btn.pri{background:#3ddc84;color:#0f0f0f;border-color:#3ddc84;font-weight:700}"
    ".btn.pri:hover{filter:brightness(1.15);color:#0f0f0f}"
    ".btn.red{background:#f44;color:#fff;border-color:#f44}"
    ".btn.red:hover{filter:brightness(1.1);color:#fff}"
    ".btn.wk{background:#e0d204;color:#0f0f0f;border-color:#e0d204}"
    ".fld{display:flex;gap:6px;align-items:center;margin:4px 0;flex-wrap:wrap}"
    ".fld label{min-width:100px;color:#888;font-size:12px;flex-shrink:0}"
    ".fld input,.fld select{background:#0f0f0f;color:#d4d4d4;border:1px solid #303030;border-radius:4px;padding:5px 7px;font:13px ui-monospace,monospace;flex:0 1 140px}"
    ".fld .unit{color:#888;font-size:11px;margin-left:-4px}"
    ".msg{color:#3ddc84;font-size:11px;margin-left:4px}.msg.err{color:#f44}"
    "pre{background:#0a0b0e;border:1px solid #303030;padding:8px;border-radius:4px;max-height:40vh;overflow:auto;font:11px/1.4 ui-monospace,monospace;margin:0;white-space:pre-wrap;color:#aaa}"
    "@media(max-width:600px){.bar h1{font-size:13px;letter-spacing:1px}.btn{font-size:12px;padding:6px 9px}.fld label{min-width:85px;font-size:11px}}"
    "</style></head><body><div class=wrap>"

    "<div class=bar>"
    "<h1>BATOFF &middot; MANUT</h1>"
    "<div class=acts>"
    "<a class=btn href='/'>Web completa</a>"
    "<a class=btn href='/update'>Firmware</a>"
    "<a class='btn red' href='/ap/off'>Desligar AP</a>"
    "</div></div>"

    "<h2>ESTADO AO VIVO</h2>"
    "<div id=live>carregando...</div>"
    "<h2>ACAO DE CICLO</h2>"
    "<div class=card><div class=fld style='gap:8px'>"
    "<button class='btn pri' onclick=\"sv('acao','testar')\">TESTAR CICLO</button>"
    "<button class='btn red' onclick=\"sv('acao','parar')\">PARAR CICLO</button>"
    "<span id=m_acao class=msg></span></div></div>"

    "<h2>TESTAR RELES</h2>"
    "<div class=card>"
    "<div class=fld><label>R1 Esteira</label>"
      "<button class=btn onclick=\"rl(0,'pulso')\">PULSO 1s</button>"
      "<button class=btn onclick=\"rl(0,'on')\">LIGA</button>"
      "<button class=btn onclick=\"rl(0,'off')\">DESLIGA</button>"
      "<span id=m_r0 class=msg></span></div>"
    "<div class=fld><label>R2 Sirene</label>"
      "<button class=btn onclick=\"rl(1,'pulso')\">PULSO 1s</button>"
      "<button class='btn wk' onclick=\"rl(1,'on')\">LIGA</button>"
      "<button class=btn onclick=\"rl(1,'off')\">DESLIGA</button>"
      "<span id=m_r1 class=msg></span></div>"
    "<div class=fld><label>R3 Livre</label>"
      "<button class=btn onclick=\"rl(2,'pulso')\">PULSO</button>"
      "<button class=btn onclick=\"rl(2,'on')\">LIGA</button>"
      "<button class=btn onclick=\"rl(2,'off')\">DESLIGA</button>"
      "<span id=m_r2 class=msg></span></div>"
    "<div class=fld><label>R4 Livre</label>"
      "<button class=btn onclick=\"rl(3,'pulso')\">PULSO</button>"
      "<button class=btn onclick=\"rl(3,'on')\">LIGA</button>"
      "<button class=btn onclick=\"rl(3,'off')\">DESLIGA</button>"
      "<span id=m_r3 class=msg></span></div>"
    "<div style='color:#888;font-size:10px;margin-top:4px'>R1 bloqueado durante ciclo. PULSO = 1s.</div>"
    "</div>"

    "<h2>TEMPOS</h2>"
    "<div class=card>"
    "<div class=fld><label>Padrao</label><input id=i_tempo type=number min=60 max=36000 step=10><span class=unit>s</span><button class=btn onclick=\"sv('tempo',i_tempo.value)\">Aplicar</button><span id=m_tempo class=msg></span></div>"
    "<div class=fld><label>MIX A</label><input id=i_mixa type=number min=60 max=36000 step=10><span class=unit>s</span><button class=btn onclick=\"sv('mixa',i_mixa.value)\">Aplicar</button><span id=m_mixa class=msg></span></div>"
    "<div class=fld><label>MIX B</label><input id=i_mixb type=number min=60 max=36000 step=10><span class=unit>s</span><button class=btn onclick=\"sv('mixb',i_mixb.value)\">Aplicar</button><span id=m_mixb class=msg></span></div>"
    "</div>"

    "<h2>MODO E AUTOMATICOS</h2>"
    "<div class=card>"
    "<div class=fld><label>Modo</label><select id=i_modo><option value=auto>AUTO</option><option value=manual>MANUAL</option></select><button class=btn onclick=\"sv('modo',i_modo.value)\">Aplicar</button><span id=m_modo class=msg></span></div>"
    "<div class=fld><label>Sirene</label><select id=i_sir><option value=0>desligada</option><option value=1>habilitada</option></select><button class=btn onclick=\"sv('sirene',i_sir.value)\">Aplicar</button><span id=m_sirene class=msg></span></div>"
    "<div class=fld><label>Horarios</label><input id=i_sirH style='flex:1 1 200px' placeholder='07:00,16:00,00:00'><button class=btn onclick=\"sv('sirHor',i_sirH.value)\">Aplicar</button><span id=m_sirHor class=msg></span></div>"
    "<div class=fld><label>Encoder fim</label><select id=i_encF><option value=0>observando</option><option value=1>ativo</option></select><button class=btn onclick=\"sv('encF',i_encF.value)\">Aplicar</button><span id=m_encF class=msg></span></div>"
    "</div>"

    "<h2>MODBUS DA IMPRESSORA</h2>"
    "<div class=card>"
    "<div class=fld><label>Funcao</label><select id=mb_fc style='flex:1 1 220px'>"
    "<option value=3>3 - holding read (4x)</option>"
    "<option value=4>4 - input read (3x)</option>"
    "<option value=1>1 - coils read (0x)</option>"
    "<option value=2>2 - discrete read (1x)</option>"
    "<option value=6>6 - holding write</option>"
    "<option value=5>5 - coil write</option></select></div>"
    "<div class=fld><label>Endereco</label><input id=mb_a type=number min=0 max=65535 value=200><label style='min-width:30px'>N/V</label><input id=mb_n type=number min=1 max=256 value=1><button class='btn pri' onclick=mbgo()>Executar</button></div>"
    "<pre id=mb_out>-</pre>"
    "<div style='font-size:10px;color:#888;line-height:1.6;margin-top:6px'>Uteis: 200=scanner | 1=cache | 60=encoder | 17=tampa | coil 2=clear</div>"
    "</div>"

    "<h2>LOG DO PLC</h2><pre id=log>carregando...</pre>"

    "<script>"
    "let primeiro=true;"
    "function msg(id,txt,erro){"
    " const el=document.getElementById(id); if(!el)return;"
    " el.textContent=txt; el.className='msg'+(erro?' err':'');"
    " setTimeout(()=>el.textContent='',3000);"
    "}"
    "async function sv(k,v){"
    " try{const r=await fetch('/set?'+k+'='+encodeURIComponent(v));"
    " const j=await r.json(); msg('m_'+k,j.msg,!j.ok);"
    " }catch(e){msg('m_'+k,e,true);}"
    "}"
    "async function rl(n,op){"
    " try{const r=await fetch('/rele?n='+n+'&v='+op);"
    " const j=await r.json(); msg('m_r'+n,j.ok?('OK '+op):j.msg,!j.ok);"
    " }catch(e){msg('m_r'+n,e,true);}"
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
    "  const cls=j.fase=='ERRO'?'er':(j.fase=='OCIOSO'?'':'wk');"
    "  document.getElementById('live').innerHTML="
    "   `<div class='card hero ${cls}'><div class=k>Fase</div><div class='v big'>${j.fase}</div></div>`+"
    "   `<div class=grid>"
    "     <div class=card><div class=k>Codigo</div><div class=v>${j.codigo||'-'}</div></div>"
    "     <div class=card><div class=k>Modo</div><div class=v>${j.modo.toUpperCase()}</div></div>"
    "     <div class=card><div class=k>Tempo</div><div class=v>${j.tempo_restante_s}s / ${j.tempo_alvo_s}s</div></div>"
    "     <div class=card><div class=k>R1 Esteira</div><div class=v>${j.rele?'ON':'off'} / ${j.esteira?'LIG':'par'}</div></div>"
    "     <div class=card><div class=k>Encoder</div><div class=v>${j.encoder}</div></div>"
    "     <div class=card><div class=k>Sirene</div><div class=v>${j.sirene_hab?'HAB':'off'}</div></div>"
    "     <div class=card><div class=k>IP PLC</div><div class=v>${j.ip_plc}</div></div>"
    "     <div class=card><div class=k>IP impr.</div><div class=v>${j.ip_impressora}</div></div>"
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
    "</script></div></body></html>"
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

// Hook pra o main sinalizar se e seguro atualizar (so em OCIOSO).
inline bool otaPermitido();

static void rotaUpdatePOST(Client& c, uint32_t contentLen) {
  if (contentLen == 0 || contentLen > 4UL * 1024UL * 1024UL) {
    c.print("HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
            "Content-Length ausente ou invalido\n");
    return;
  }
  if (!otaPermitido()) {
    c.print("HTTP/1.1 409 Conflict\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
            "Nao e seguro atualizar durante um ciclo em andamento.\n"
            "Espere o ciclo terminar (fase OCIOSO) e tente de novo.\n");
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

// GET /rele?n=X&v=on|off|pulso  - aciona rele X (0..3) manualmente
static void rotaRele(Client& c, const String& q) {
  int n = param(q, "n").toInt();
  String v = param(q, "v");
  int op = -1;
  if (v == "off" || v == "0") op = 0;
  else if (v == "on"  || v == "1") op = 1;
  else if (v == "pulso" || v == "p") op = 2;
  else { respondeJson(c, "{\"ok\":false,\"msg\":\"v=on|off|pulso\"}"); return; }
  int r = manutReleAciona(n, op);
  char buf[160];
  if (r == 0) snprintf(buf, sizeof buf, "{\"ok\":true,\"n\":%d,\"op\":\"%s\"}", n, v.c_str());
  else if (r == 1) snprintf(buf, sizeof buf, "{\"ok\":false,\"msg\":\"canal invalido (use 0..3)\"}");
  else snprintf(buf, sizeof buf, "{\"ok\":false,\"msg\":\"R%d bloqueado durante ciclo\"}", n + 1);
  respondeJson(c, buf);
}

static bool manutencaoRota(Client& c, const String& metodo, const String& caminho,
                           const String& q, uint32_t contentLen) {
  if (caminho == "/manut")      { rotaManutHTML(c); return true; }
  if (caminho == "/live.json")  { rotaLiveJson(c); return true; }
  if (caminho == "/log.txt")    { rotaLogTxt(c); return true; }
  if (caminho == "/set")        { rotaSet(c, q); return true; }
  if (caminho == "/mb")         { rotaModbus(c, q); return true; }
  if (caminho == "/rele")       { rotaRele(c, q); return true; }
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
