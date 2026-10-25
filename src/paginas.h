#pragma once
// Paginas HTML do StamPLC (painel, relatorios, eventos, ajustes).
// Incluir no main.cpp depois de tela.h.

static void pararTudo();
static void mudaTempo(int delta);
static void disparar(const char* motivo, const char* codigo);

// valor de um parametro da URL; confere o nome inteiro ("ip" nao casa com "pip")
static String param(const String& q, const char* k) {
  String key = String(k) + "=";
  int i = q.startsWith(key) ? 0 : q.indexOf(String("&") + key);
  if (i < 0) return "";
  if (i > 0) i++;
  i += key.length();
  int j = q.indexOf('&', i);
  String v = j < 0 ? q.substring(i) : q.substring(i, j);
  v.replace("+", " ");
  return v;
}

static String esc(const String& s) {
  String r;
  for (unsigned i = 0; i < s.length(); i++) {
    char ch = s[i];
    if (ch == '<') r += "&lt;";
    else if (ch == '>') r += "&gt;";
    else if (ch == '&') r += "&amp;";
    else if (ch == '"' || ch == '\'') r += "&#39;";
    else r += ch;
  }
  return r;
}

static const char CSS1[] =
    ":root{--bg:#f6f7fa;--card:#fff;--txt:#1c202a;--mut:#6e7686;--sep:#e5e9f0;--ok:#108844;--okb:#dcf3e3;--wk:#b97300;--wkb:#feefcb;--er:#c02020;--erb:#fcdbdb;--brand:#111;--brandr:#d62828;--acc:#2563eb;--accb:#eaf1ff}"
    "*{box-sizing:border-box}html,body{margin:0;padding:0}body{font:15px/1.5 -apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Helvetica,Arial,sans-serif;background:var(--bg);color:var(--txt);min-height:100vh}"
    ".topbar{background:#fff;border-bottom:1px solid var(--sep);position:sticky;top:0;z-index:9;box-shadow:0 1px 3px #0001}"
    ".tb{max-width:1080px;margin:0 auto;display:flex;align-items:center;padding:10px 16px;gap:16px}"
    ".logo{font-weight:800;letter-spacing:.5px;font-size:17px;white-space:nowrap}"
    ".logo b{color:var(--brand)}.logo i{color:var(--brandr);font-style:normal}"
    ".logo s{text-decoration:none;color:var(--mut);font-weight:500;font-size:13px;margin-left:8px}"
    "nav{display:flex;gap:2px;overflow-x:auto;margin-left:auto}"
    "nav a{color:var(--mut);text-decoration:none;padding:8px 14px;border-radius:8px;font-weight:600;font-size:14px;white-space:nowrap;transition:background .15s}"
    "nav a:hover{background:var(--acc);background:var(--accb);color:var(--acc)}"
    "nav a.on{background:var(--brand);color:#fff}"
    "main{max-width:1080px;margin:0 auto;padding:18px 16px 40px}"
    "h1{font-size:16px;text-transform:uppercase;letter-spacing:.6px;color:var(--mut);margin:24px 0 10px;font-weight:600}h1:first-child{margin-top:4px}"
    ".st{border-radius:14px;padding:18px 22px;margin-bottom:16px;border-left:5px solid}"
    ".st b{display:block;font-size:30px;line-height:1.1;margin-bottom:4px;font-weight:700}"
    ".st span{font-size:15px;opacity:.85}"
    ".st.ok{background:var(--okb);color:var(--ok);border-color:var(--ok)}"
    ".st.wk{background:var(--wkb);color:var(--wk);border-color:var(--wk)}"
    ".st.er{background:var(--erb);color:var(--er);border-color:var(--er)}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:10px}"
    ".card{background:var(--card);border-radius:12px;padding:14px 16px;border:1px solid var(--sep)}"
    ".card small{display:block;color:var(--mut);font-size:11px;text-transform:uppercase;letter-spacing:.6px;margin-bottom:4px;font-weight:600}"
    ".card b{font-size:24px;font-weight:700;line-height:1.2}";

static const char CSS2[] =
    "table{width:100%;border-collapse:collapse;background:var(--card);border-radius:12px;overflow:hidden;border:1px solid var(--sep);font-size:14px}"
    "td,th{padding:10px 14px;border-bottom:1px solid var(--sep);text-align:left;vertical-align:top}"
    "tr:last-child td{border-bottom:0}"
    "th{background:#fafbfd;font-size:11px;text-transform:uppercase;color:var(--mut);font-weight:600;letter-spacing:.5px}"
    "tr.cod td{font-weight:700;background:var(--accb)}tr.err td{color:var(--er);font-weight:600}"
    "tr.dim td{color:var(--mut)}tr.on td:nth-child(2){color:var(--ok);font-weight:700}"
    "tr.off td:nth-child(2){color:var(--er);font-weight:700}"
    ".btn{display:inline-block;background:var(--acc);color:#fff;border:0;border-radius:10px;padding:11px 20px;font:600 15px -apple-system,Segoe UI,sans-serif;text-decoration:none;cursor:pointer;transition:transform .1s}"
    ".btn:active{transform:scale(.98)}.btn.red{background:var(--er)}"
    ".btn.sec{background:#eff2f6;color:var(--txt);border:1px solid var(--sep)}"
    ".btn.sec:hover{background:#e5e9f0}"
    ".rel{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}"
    ".rel div{text-align:center;padding:14px 8px;border-radius:10px;background:#eff2f6;color:var(--mut);font-weight:700;border:1px solid var(--sep)}"
    ".rel div.on{background:var(--ok);color:#fff;border-color:var(--ok)}"
    ".rel div small{font-weight:400;font-size:11px;display:block;margin-top:3px;text-transform:uppercase;letter-spacing:.4px}"
    ".alert{background:var(--wkb);color:#6b4a00;border-radius:10px;padding:11px 15px;margin-bottom:12px;border-left:4px solid var(--wk);font-size:14px}"
    ".alert.ok{background:var(--okb);color:var(--ok);border-color:var(--ok)}"
    ".alert a{color:inherit;font-weight:700}"
    ".dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:10px}"
    ".dot.ok{background:var(--ok);box-shadow:0 0 0 3px var(--okb)}"
    ".dot.er{background:var(--er);box-shadow:0 0 0 3px var(--erb)}"
    ".mono{font-family:ui-monospace,SF Mono,Monaco,monospace;font-size:14px}"
    "input,select{font:15px system-ui;padding:10px 12px;width:100%;max-width:320px;border:1px solid var(--sep);border-radius:8px;background:#fff}"
    "input:focus,select:focus{outline:0;border-color:var(--acc);box-shadow:0 0 0 3px var(--accb)}"
    "label{display:block;margin:14px 0 5px;font-weight:600;font-size:14px}"
    ".nota{color:var(--mut);font-size:13px;margin:4px 0}"
    ".row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin:8px 0}"
    ".big{font-size:22px;font-weight:700}"
    "@media(max-width:620px){.tb{padding:8px 12px;gap:8px}.logo s{display:none}"
    "nav a{padding:7px 10px;font-size:13px}.st b{font-size:24px}main{padding:12px}}";

static void http200(EthernetClient& c) {
  c.print("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
}

static void redireciona(EthernetClient& c, const char* destino) {
  c.print("HTTP/1.1 303 See Other\r\nLocation: ");
  c.print(destino);
  c.print("\r\nConnection: close\r\n\r\n");
}

static void abre(EthernetClient& c, const char* ativo, const char* titulo) {
  http200(c);
  c.print("<!doctype html><html lang='pt-br'><head><meta charset='utf-8'>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'><title>");
  c.print(titulo);
  c.print("</title><style>");
  c.print(CSS1);
  c.print(CSS2);
  c.print("</style></head><body><div class='topbar'><div class='tb'><div class='logo'><b>CARGA</b> <i>POSITIVA</i><s>Painel do equipamento</s></div><nav>");
  static const char* ids[] = {"painel", "hoje", "dias", "eventos", "config"};
  static const char* hr[] = {"/", "/hoje", "/relatorio", "/eventos", "/config"};
  static const char* nm[] = {"Painel", "Hoje", "Dias anteriores", "Eventos", "Ajustes"};
  for (int i = 0; i < 5; i++)
    c.printf("<a href='%s'%s>%s</a>", hr[i], !strcmp(ativo, ids[i]) ? " class='on'" : "", nm[i]);
  c.print("</nav></div></div><main>");
}

static void fecha(EthernetClient& c, bool atualiza) {
  // ah(data, voltarAoPainel): manda a data/hora para o equipamento
  c.print("<script>function ah(d,v){location='/hora?'+(v?'volta=/&':'')+'a='+d.getFullYear()+'&m='+(d.getMonth()+1)"
          "+'&d='+d.getDate()+'&h='+d.getHours()+'&i='+d.getMinutes()+'&s='+d.getSeconds()}</script>");
  // hora nunca acertada: acerta sozinho, uma vez por sessao do navegador
  if (!horaOk)
    c.print("<script>try{if(!sessionStorage.getItem('hs')){sessionStorage.setItem('hs','1');ah(new Date(),true)}}catch(e){}</script>");
  if (atualiza) c.print("<script>setTimeout(function(){if(!document.hidden)location.reload()},2500)</script>");
  c.print("</main></body></html>");
}

static const char* clsNivel(int n) { return n == 0 ? "ok" : n == 1 ? "wk" : "er"; }

static void linhaConexao(EthernetClient& c, bool ok, const char* nome, const String& valor) {
  c.printf("<tr><td><span class='dot %s'></span>%s</td><td>%s</td></tr>", ok ? "ok" : "er", nome, valor.c_str());
}

static void cartoesResumo(EthernetClient& c, const Resumo& r) {
  long tot = r.ligada + r.parada;
  c.printf("<div class='grid'><div class='card'><small>Codigos lidos</small><b>%d</b></div>"
           "<div class='card'><small>Esteira ligada</small><b style='color:var(--ok)'>%s</b></div>"
           "<div class='card'><small>Esteira parada</small><b style='color:var(--er)'>%s</b></div>",
           r.codigos, duracao(r.ligada).c_str(), duracao(r.parada).c_str());
  if (tot > 0) c.printf("<div class='card'><small>Esteira ligada (%% do tempo)</small><b>%ld%%</b></div>", r.ligada * 100 / tot);
  if (r.falhas) c.printf("<div class='card'><small>Falhas ao apagar</small><b style='color:var(--er)'>%d</b></div>", r.falhas);
  c.print("</div>");
}

// ---- Painel ---------------------------------------------------------------------
static void paginaPainel(EthernetClient& c) {
  abre(c, "painel", "Painel");
  if (!horaOk)
    c.print("<div class='alert'>A hora do equipamento ainda nao foi acertada; os relatorios ficam sem data. "
            "<a href='/config'>Acertar agora</a></div>");
  if (!sdOk) c.print("<div class='alert'>Cartao SD nao encontrado: o relatorio do dia nao esta sendo gravado.</div>");
  if (fonteV < 6.0f)
    c.printf("<div class='alert'>Alimentacao em %.1f V: os reles so acionam com 6 a 36 V.</div>", fonteV);

  Status s = statusAtual();
  c.printf("<div class='st %s'><b>%s</b><span>%s</span></div>", clsNivel(s.nivel), s.titulo, s.detalhe);

  c.print("<div class='card'><small>Ultimo codigo lido</small><b class='mono'>");
  if (ultCodigo[0]) {
    c.print(esc(ultCodigo));
    c.printf("</b> <span class='nota'>as %s</span></div>", ultHora);
  } else {
    c.print("nenhum ainda</b></div>");
  }

  c.print("<h1>Hoje</h1>");
  cartoesResumo(c, resHoje);
  c.printf("<p class='nota'>Esteira ligada = %s.</p>", esteiraDesc().c_str());

  c.printf("<h1>Tempo do rele</h1><div class='card' style='display:flex;align-items:center;gap:10px;flex-wrap:wrap'>"
           "<span>Esteira roda por <b style='font-size:24px'>%.0f min</b></span>"
           "<a class='btn sec' href='/tempo?v=menos'>&minus; 1 min</a><a class='btn sec' href='/tempo?v=mais'>+ 1 min</a></div>",
           esteiraSegMs / 60000.0);
  c.print("<h1>Reles</h1><div class='rel'>");
  for (int i = 0; i < 4; i++)
    c.printf("<div class='%s'>R%d<small>%s</small></div>", relEst[i] ? "on" : "", i + 1, relEst[i] ? "FECHADO" : "aberto");
  c.printf("</div><p class='nota'>O rele <b>R%d</b> fecha quando o codigo e lido e abre depois de %.0f min.</p>",
           RELE_CANAL + 1, esteiraSegMs / 60000.0);

  c.print("<h1>Conexoes</h1><table>");
  linhaConexao(c, linkOk, "Cabo de rede", linkOk ? "conectado" : "desconectado");
  linhaConexao(c, online, "Impressora", ipImpressora.toString() + (online ? " - respondendo" : " - sem resposta"));
  linhaConexao(c, sdOk, "Cartao SD", sdOk ? "gravando o relatorio" : "nao encontrado");
  char d[16], h[10];
  carimbo(d, h);
  linhaConexao(c, horaOk, "Hora do equipamento", horaOk ? String(d) + " " + h : String("nao acertada"));
  c.print("</table>");

  c.print("<div class='row' style='margin-top:20px'>"
          "<a class='btn' href='/testar' onclick=\"return confirm('Simular uma leitura?')\">&#9654; Testar (simular leitura)</a>"
          "<a class='btn red' href='/parar'>&#9632; Parar e abrir o rele</a></div>");
  fecha(c, true);
}

// ---- Relatorio de um dia ---------------------------------------------------------
static String nomeEvento(const String& e, const char*& cls) {
  cls = "";
  if (e == "CODIGO") { cls = "cod"; return "Codigo lido"; }
  if (e == "APAGADO") return "Codigo apagado da impressora";
  if (e == "FALHA_APAGAR") { cls = "err"; return "Nao consegui apagar o codigo"; }
  if (e == "ESTEIRA_LIGADA") { cls = "on"; return "Esteira LIGADA"; }
  if (e == "ESTEIRA_PARADA") { cls = "off"; return "Esteira PARADA"; }
  if (e == "IMPRESSORA_ONLINE") { cls = "dim"; return "Impressora conectada"; }
  if (e == "IMPRESSORA_OFFLINE") { cls = "err"; return "Impressora sem comunicacao"; }
  if (e == "ALARME") { cls = "err"; return "Alarme da impressora"; }
  if (e == "INICIO") { cls = "dim"; return "Equipamento ligado"; }
  if (e == "HORA_ACERTADA") { cls = "dim"; return "Hora acertada"; }
  if (e == "DIA_INICIO" || e == "DIA_FIM") { cls = "dim"; return "Virada do dia"; }
  return e;
}

static void paginaDia(EthernetClient& c, const String& dia, const char* ativo) {
  char hojeS[16], h[10];
  carimbo(hojeS, h);
  bool hoje = dia == hojeS;
  abre(c, ativo, "Relatorio do dia");
  c.printf("<h1>Relatorio de %s%s</h1>", dia.c_str(), hoje ? " (hoje)" : "");
  if (!SD.exists("/rel/" + dia + ".csv")) {
    c.print("<p>Nao ha registros neste dia.</p>");
    fecha(c, false);
    return;
  }
  cartoesResumo(c, resumoDoDia(dia, hoje));
  c.printf("<p><a class='btn sec' href='/relatorio.csv?dia=%s'>Baixar para o Excel (CSV)</a> "
           "<a class='nota' href='#fim'>ir para o fim &darr;</a></p>",
           dia.c_str());
  c.print("<table><tr><th>Hora</th><th>O que aconteceu</th><th>Codigo de barras</th></tr>");
  File f = SD.open("/rel/" + dia + ".csv");
  String l, v[4];
  lerLinha(f, l);
  while (lerLinha(f, l)) {
    if (campos(l, v, 4) < 2 || v[0] == "hora") continue;  // pula cabecalho repetido
    const char* cls;
    String nome = nomeEvento(v[1], cls);
    String extra = v[3].length() && v[1] != "CODIGO" && v[1] != "DIA_INICIO" && v[1] != "HORA_ACERTADA" ? " (" + esc(v[3]) + ")" : "";
    c.printf("<tr class='%s'><td>%s</td><td>%s%s</td><td class='mono'>%s</td></tr>", cls, v[0].c_str(), nome.c_str(),
             extra.c_str(), esc(v[2]).c_str());
  }
  f.close();
  c.print("</table><div id='fim'></div>");
  fecha(c, hoje);
}

static void paginaIndice(EthernetClient& c) {
  abre(c, "dias", "Dias anteriores");
  c.print("<h1>Relatorio de cada dia</h1>");
  if (!sdOk) {
    c.print("<div class='alert'>Cartao SD nao encontrado: nao ha relatorios para mostrar.</div>");
  } else {
    String dias[90];
    int n = 0;
    File root = SD.open("/rel");
    for (File e = root.openNextFile(); e && n < 90; e = root.openNextFile()) {
      String nm = e.name();
      if (nm.endsWith(".csv")) dias[n++] = nm.substring(0, nm.length() - 4);
      e.close();
    }
    root.close();
    for (int i = 0; i < n; i++)  // mais recente primeiro
      for (int j = i + 1; j < n; j++)
        if (dias[j] > dias[i]) { String t = dias[i]; dias[i] = dias[j]; dias[j] = t; }
    if (!n) c.print("<p>Nenhum registro ainda.</p>");
    c.print("<table><tr><th>Dia</th><th>Codigos</th><th>Esteira ligada</th><th>Esteira parada</th></tr>");
    for (int i = 0; i < n; i++) {
      Resumo r = resumoDoDia(dias[i], false);
      c.printf("<tr><td><a href='/relatorio?dia=%s'><b>%s</b></a></td><td>%d</td><td>%s</td><td>%s</td></tr>", dias[i].c_str(),
               dias[i].c_str(), r.codigos, duracao(r.ligada).c_str(), duracao(r.parada).c_str());
    }
    c.print("</table>");
  }
  fecha(c, false);
}

// ---- Eventos (registro tecnico) ----------------------------------------------------
static void paginaEventos(EthernetClient& c) {
  abre(c, "eventos", "Eventos");
  c.print("<h1>Registro tecnico</h1><p class='nota'>O que o equipamento fez desde que ligou, mais recente primeiro. "
          "Serve para suporte; o relatorio do dia fica na aba Hoje.</p><table><tr><th>Hora</th><th>Evento</th></tr>");
  for (int i = 0; i < LOG_N; i++) {
    const char* l = logBuf[(logPos - 1 - i + LOG_N * 2) % LOG_N];
    if (!l[0]) continue;
    const char* sp = strchr(l, ' ');
    String hora = sp ? String(l).substring(0, sp - l) : "";
    c.printf("<tr><td>%s</td><td>%s</td></tr>", hora.c_str(), esc(sp ? sp + 1 : l).c_str());
  }
  c.print("</table>");
  fecha(c, true);
}

// ---- Ajustes ---------------------------------------------------------------------------
static void paginaConfig(EthernetClient& c, const char* erro, const char* sucesso = nullptr) {
  abre(c, "config", "Ajustes");
  c.print("<h1>Ajustes</h1>");
  if (sucesso) c.printf("<div class='alert ok'>%s</div>", sucesso);
  if (erro) c.printf("<div class='alert'>%s</div>", erro);
  c.printf("<form action='/salvar'><h1>Rele</h1>"
           "<label>Tempo da esteira rodando (minutos)</label><input name='tempo' value='%.0f'>"
           "<p class='nota'>Entre 1 e 600 minutos. Tambem da para mudar direto na tela do equipamento (tela 5, botoes B e C).</p>"
           "<label>Tolerancia se esteira cair no meio (minutos)</label><input name='espera' value='%u'>"
           "<p class='nota'>Se a esteira parar durante o ciclo, o PLC espera este tempo pelo operador religar. Se religar, continua. Se nao, apaga o codigo e cancela.</p>"
           "<h1>Esteira</h1><label>Como saber se a esteira esta ligada?</label><select name='eent'>",
           esteiraSegMs / 60000.0);
  c.printf("<option value='-1'%s>Pelo rele (R%d fechado = esteira ligada)</option>", esteiraEntrada < 0 ? " selected" : "", RELE_CANAL + 1);
  for (int i = 0; i < 8; i++) c.printf("<option value='%d'%s>Pela entrada %d do StamPLC</option>", i, esteiraEntrada == i ? " selected" : "", i + 1);
  c.printf("</select><label>Nivel do sinal da entrada</label><select name='ealto'>"
           "<option value='1'%s>Com tensao = esteira ligada</option><option value='0'%s>Sem tensao = esteira ligada</option></select>"
           "<h1>Rede</h1><label>IP da impressora</label><input name='pip' value='%s'>"
           "<label>IP deste equipamento (StamPLC)</label><input name='ip' value='%s'>"
           "<label>Gateway</label><input name='gw' value='%s'>"
           "<p><button class='btn'>Salvar e reiniciar</button></p></form>",
           esteiraAlto ? " selected" : "", esteiraAlto ? "" : " selected",
           ipImpressora.toString().c_str(), ipLocal.toString().c_str(), ipGw.toString().c_str());
  char d[16], h[10];
  carimbo(d, h);
  c.printf("<h1>Hora do equipamento</h1><p>Agora: <b>%s %s</b></p>", horaOk ? d : "(nao acertada)", h);
  c.print("<p><button class='btn sec' onclick=\"ah(new Date())\">Acertar com o relogio deste aparelho</button></p>"
          "<p class='nota'>Ou digite a data e a hora certas:</p>"
          "<input type='datetime-local' id='dt'> "
          "<button class='btn sec' onclick=\"var v=document.getElementById('dt').value;"
          "if(!v){alert('Escolha a data e a hora');return}ah(new Date(v))\">Acertar com esta hora</button>");
  fecha(c, false);
}

// ---- Atendimento ---------------------------------------------------------------------------
static void atendeWeb() {
  EthernetClient c = web.available();
  if (!c) return;
  unsigned long t0 = millis();
  String linha;
  while (c.connected() && millis() - t0 < 300) {
    if (!c.available()) continue;
    char ch = c.read();
    if (ch == '\n') break;
    if (ch != '\r' && linha.length() < 200) linha += ch;
  }
  while (c.available()) c.read();  // descarta o resto do cabecalho

  String caminho = "/", q = "";
  if (linha.startsWith("GET ")) {
    int fim = linha.indexOf(" HTTP");
    String alvo = fim > 4 ? linha.substring(4, fim) : "/";
    int qi = alvo.indexOf('?');
    if (qi >= 0) { caminho = alvo.substring(0, qi); q = alvo.substring(qi + 1); }
    else caminho = alvo;
  }
  String dia = param(q, "dia");
  for (unsigned i = 0; i < dia.length(); i++)  // so letras minusculas, digitos e hifen
    if (!(isdigit(dia[i]) || dia[i] == '-' || (dia[i] >= 'a' && dia[i] <= 'z'))) dia = "";
  if (dia.length() > 15) dia = "";

  if (caminho == "/") {
    paginaPainel(c);
  } else if (caminho == "/hoje") {
    char d[16], h[10];
    carimbo(d, h);
    paginaDia(c, String(d), "hoje");
  } else if (caminho == "/relatorio") {
    if (dia.length()) paginaDia(c, dia, "dias");
    else paginaIndice(c);
  } else if (caminho == "/relatorio.csv") {
    enviaCsv(c, dia);
  } else if (caminho == "/eventos") {
    paginaEventos(c);
  } else if (caminho == "/config") {
    char d[16], h[10];
    carimbo(d, h);
    String okMsg = param(q, "ok") == "hora" ? String("Hora acertada: ") + d + " " + h : String("");
    paginaConfig(c, param(q, "erro") == "hora" ? "Nao consegui acertar a hora: valores invalidos." : nullptr,
                 okMsg.length() ? okMsg.c_str() : nullptr);
  } else if (caminho == "/testar") {
    if (fase == OCIOSO) disparar("teste pelo painel", "TESTE-PAINEL");
    redireciona(c, "/");
  } else if (caminho == "/tempo") {
    mudaTempo(param(q, "v") == "mais" ? 60000 : -60000);
    redireciona(c, "/");
  } else if (caminho == "/parar") {
    pararTudo();
    redireciona(c, "/");
  } else if (caminho == "/hora") {
    bool ok = relogioAcerta(param(q, "a").toInt(), param(q, "m").toInt(), param(q, "d").toInt(), param(q, "h").toInt(),
                            param(q, "i").toInt(), param(q, "s").toInt());
    if (ok) {
      relEvento("HORA_ACERTADA", "", esteiraLigada ? "ligada" : "parada");
      avisof("Hora acertada");
      relVirada(esteiraLigada);
    }
    redireciona(c, !ok ? "/config?erro=hora" : param(q, "volta") == "/" ? "/" : "/config?ok=hora");
  } else if (caminho == "/salvar") {
    IPAddress tmp, i1, i2, i3;
    String pip = param(q, "pip"), ip = param(q, "ip"), gw = param(q, "gw"), tempo = param(q, "tempo");
    tempo.replace(",", ".");
    long ms = lround(tempo.toFloat() * 60000.0f);
    long espms = param(q, "espera").toInt() * 60000L;
    if (espms < 10000) espms = 60000;  // minimo 10s, default 1 min
    int eent = param(q, "eent").toInt();
    if (i1.fromString(pip) && i2.fromString(ip) && i3.fromString(gw) && ms >= 60000 && ms <= 36000000L && eent >= -1 && eent <= 7) {
      prefs.putString("pip", pip);
      prefs.putString("ip", ip);
      prefs.putString("gw", gw);
      prefs.putUInt("etempo", (uint32_t)ms);
      prefs.putInt("eent", eent);
      prefs.putBool("ealto", param(q, "ealto") != "0");
      prefs.putUInt("esper", (uint32_t)espms);
      abre(c, "config", "Salvo");
      c.printf("<h1>Salvo</h1><p>O equipamento esta reiniciando. Em cerca de 10 segundos voce sera levado para "
               "<b>http://%s/</b>.</p><script>setTimeout(function(){location='http://%s/'},12000)</script>",
               ip.c_str(), ip.c_str());
      fecha(c, false);
      c.stop();
      logf("ajustes salvos; reiniciando");
      delay(500);
      ESP.restart();
    }
    paginaConfig(c, "Valores invalidos: confira os enderecos IP e use um tempo entre 1 e 600 segundos.");
  } else {
    c.print("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
  }
  c.stop();
}
