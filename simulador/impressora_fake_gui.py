"""
Simulador da impressora Codeline SS5632
=======================================

Reproduz o comportamento real da impressora que descobrimos na fabrica,
servindo Modbus TCP (porta 1502) e o protocolo privado (porta 4008).

Útil para testar o StamPLC em bancada, sem a impressora fisica.

Autor: Ramon
Repo:  https://github.com/Ramon-6/CodelineSS5632

O que o simulador faz:
  - Modbus TCP completo (fc=01, 02, 03, 04, 05, 06, 16) nos registros
    documentados no manual + os que descobrimos
  - Protocolo privado da porta 4008 (comandos 0x33 reload template,
    0x10 clear cache, 0x4D heartbeat, 0x3F query counter, 0x4C device
    name, 0x16 callback etc)
  - Simula leitura de scanner (digita no campo e vira "codigo lido")
  - Simula impressao (incrementa contador 4x0061 e dispara callback 0x16)
  - Simula alarmes (toggles) e estados (jet on/off, printer ready)
  - Visualiza em tempo real todos os registros Modbus importantes
  - Log de atividade com cores por origem (Modbus/Privado/Sistema)
"""
import socket
import socketserver
import struct
import threading
import time
import tkinter as tk
from tkinter import font, ttk

APP_VERSION = "2.0"
AUTHOR = "Ramon"
REPO = "github.com/Ramon-6/CodelineSS5632"
MODBUS_PORT = 1502
PRIV_PORT = 4008
TEMPLATE_PADRAO = "sumi rico"

# ================================================================
#                       Paleta de cores
# ================================================================
C_BG = "#0f1419"
C_PANEL = "#1b2027"
C_PANEL2 = "#242b35"
C_BORDER = "#2f3845"
C_TXT = "#e8ebf0"
C_DIM = "#8792a3"
C_ACCENT = "#4f8cff"
C_OK = "#3dd68c"
C_WARN = "#f7b955"
C_ERR = "#ff6b6b"
C_MODBUS = "#67d8ef"
C_PRIV = "#c586ff"
C_SYS = "#8792a3"
C_HIGHLIGHT = "#2d3443"


# ================================================================
#                       Estado da impressora
# ================================================================
lock = threading.Lock()
holding = {}          # 4xXXXX
input_regs = {}       # 3xXXXX
coils = {}            # 0xXXXX
discrete = {}         # 1xXXXX
contador = 25222      # mesmo valor inicial que vimos na impressora real
callback_hab = False  # callback 0x16 habilitado?
template_atual = TEMPLATE_PADRAO
jet_on = True
printer_ready = True
historico = []        # (hora, categoria, descricao)
callbacks_pendentes = []  # pacotes a serem enviados aos clientes 4008

# Valores iniciais realistas (dos exemplos vistos na impressora de verdade)
input_regs[0] = 100          # cache size
input_regs[1] = 0            # cache quantity
input_regs[2] = 2            # mix tank level
input_regs[3] = 85           # % tinta
input_regs[4] = 92           # % solvente
input_regs[5] = 70           # viscosidade
input_regs[14] = 25          # temp tinta
input_regs[15] = 34          # temp eletrica
input_regs[16] = 27          # temp cabeca
input_regs[17] = 29          # temp ambiente
input_regs[20] = 2           # machine state = running
discrete[99] = 1             # printer ready

ignora_log_regs = {60, 61}   # evita spam de contador em log


def registrar(cat, msg):
    historico.append((time.strftime("%H:%M:%S"), cat, msg))
    if len(historico) > 300:
        del historico[:50]


def set_codigo(txt):
    """Simula o leitor USB preenchendo o template da impressora."""
    with lock:
        holding[199] = 0xFFFF if txt else 0  # flag "tem codigo" (4x0200)
        for i in range(20):
            if i < len(txt):
                holding[200 + i] = ord(txt[i])
            elif i == len(txt) and txt:
                holding[200 + i] = 0x09
            else:
                holding[200 + i] = 0
        input_regs[1] = 1 if txt else 0  # cache quantity
    if txt:
        registrar("sys", f"codigo lido pelo scanner: {txt}")
    else:
        registrar("sys", "codigo apagado do template")


def ler_codigo_atual():
    with lock:
        s = ""
        for i in range(20):
            v = holding.get(200 + i, 0) & 0xFF
            if v == 0 or v == 0x09:
                break
            if 32 <= v < 127:
                s += chr(v)
        return s


def imprimir_fake():
    """Simula produto passando pela fotocelula (contador +1 + callback 0x16)."""
    global contador
    with lock:
        contador += 1
        registrar("sys", f"IMPRIMIU - contador agora {contador}")
        if callback_hab:
            # pacote de callback: 1B 1F 04 00 16 00 1C 17
            callbacks_pendentes.append(bytes([0x1B, 0x1F, 0x04, 0x00, 0x16, 0x00, 0x1C, 0x17]))


def set_alarme(bit, ativo):
    """Ativa/desativa um alarme (bit 1..32 do manual)."""
    with lock:
        discrete[bit] = 1 if ativo else 0  # 1xXXXX
        # bit 1 sempre reflete "tem algum alarme"
        algum = any(discrete.get(i, 0) for i in range(2, 54))
        discrete[0] = 1 if algum else 0
    registrar("sys", f"alarme bit {bit} = {'ON' if ativo else 'off'}")


# ================================================================
#                       Modbus TCP (porta 1502)
# ================================================================
def _pack_bits(bits):
    """Transforma lista de bits em bytes big-endian para resposta Modbus."""
    out = bytearray((len(bits) + 7) // 8)
    for i, b in enumerate(bits):
        if b:
            out[i // 8] |= (1 << (i % 8))
    return bytes(out)


def processar_pdu(fc, data):
    """Processa um PDU Modbus. Retorna o PDU de resposta."""
    global contador
    with lock:
        try:
            if fc == 1 or fc == 2:  # read coils / discrete inputs
                a, q = struct.unpack(">HH", data[:4])
                src = coils if fc == 1 else discrete
                bits = [src.get(a + i, 0) for i in range(q)]
                payload = _pack_bits(bits)
                registrar("mb", f"fc{fc} addr={a} qty={q}")
                return bytes([fc, len(payload)]) + payload
            if fc == 3 or fc == 4:  # read holding / input
                a, q = struct.unpack(">HH", data[:4])
                src = holding if fc == 3 else input_regs
                # atualiza contador antes de ler
                if fc == 3 and a <= 60 and a + q > 61:
                    src[60] = (contador >> 16) & 0xFFFF
                    src[61] = contador & 0xFFFF
                vals = [src.get(a + i, 0) for i in range(q)]
                # loga só se não for registro ignorado
                if not (a <= 60 and a + q >= 62 and q <= 2):
                    registrar("mb", f"fc{fc:02d} addr={a} qty={q}")
                return bytes([fc, q * 2]) + struct.pack(">" + "H" * q, *vals)
            if fc == 5:  # write single coil
                a, v = struct.unpack(">HH", data[:4])
                coils[a] = 1 if v == 0xFF00 else 0
                registrar("mb", f"fc05 coil {a} = {coils[a]}")
                # tratamentos especiais
                if a == 1 and coils[1]:
                    registrar("sys", "clear network cache")
                    coils[1] = 0
                return bytes([fc]) + data[:4]
            if fc == 6:  # write single holding
                a, v = struct.unpack(">HH", data[:4])
                holding[a] = v
                registrar("mb", f"fc06 holding {a} = {v}")
                return bytes([fc]) + data[:4]
            if fc == 16:  # write multiple holding
                a, q = struct.unpack(">HH", data[:4])
                for i in range(q):
                    holding[a + i] = (data[5 + i * 2] << 8) | data[6 + i * 2]
                registrar("mb", f"fc16 addr={a} qty={q}")
                return bytes([fc]) + data[:4]
            if fc == 15:  # write multiple coils
                a, q = struct.unpack(">HH", data[:4])
                nb = data[4]
                for i in range(q):
                    coils[a + i] = (data[5 + i // 8] >> (i % 8)) & 1
                registrar("mb", f"fc15 addr={a} qty={q}")
                return bytes([fc]) + data[:4]
        except Exception:
            pass
    return bytes([fc | 0x80, 1])  # Illegal function


class ModbusHandler(socketserver.BaseRequestHandler):
    def handle(self):
        peer = self.client_address[0]
        registrar("sys", f"cliente conectou (Modbus) {peer}")
        try:
            while True:
                h = self.request.recv(7)
                if len(h) < 7:
                    return
                tid, _, ln, unit = struct.unpack(">HHHB", h)
                body = self.request.recv(ln - 1)
                r = processar_pdu(body[0], body[1:])
                self.request.sendall(struct.pack(">HHHB", tid, 0, len(r) + 1, unit) + r)
        except OSError:
            pass
        finally:
            registrar("sys", f"cliente desconectou (Modbus) {peer}")


# ================================================================
#                       Porta 4008 (protocolo privado)
# ================================================================
def processar_privado(pkt, request):
    """Processa pacote da porta 4008 e envia resposta."""
    global callback_hab, template_atual, jet_on
    if len(pkt) < 8 or pkt[0] != 0x1B or pkt[1] != 0x1F:
        return
    cmd = pkt[4]
    datalen = pkt[2] | (pkt[3] << 8)
    data = pkt[5:5 + datalen]
    mode = pkt[5 + datalen] if 5 + datalen < len(pkt) else 0

    def resp(cmd, data=b"", mode=0):
        pkt = bytes([0x1B, 0x1F, len(data) & 0xFF, (len(data) >> 8) & 0xFF, cmd])
        pkt += data + bytes([mode, 0x1C, 0x17])
        request.sendall(pkt)

    if cmd == 0x0A:  # query status
        registrar("priv", "query status")
        resp(0x0B if jet_on else 0x0C)
    elif cmd == 0x11:  # enable printing
        registrar("priv", "enable printing")
        jet_on = True
        resp(0x0B)
    elif cmd == 0x04:  # disable printing
        registrar("priv", "disable printing")
        jet_on = False
        resp(0x0C)
    elif cmd == 0x43:  # open template (não recarrega)
        nome = data.decode(errors="replace")
        registrar("priv", f"open template '{nome}' (nao recarrega)")
        resp(0x43, data)
    elif cmd == 0x33:  # FORCE reload template
        nome = data.decode(errors="replace")
        registrar("priv", f"RELOAD template '{nome}' - apagando codigo")
        set_codigo("")
        template_atual = nome
        resp(0x33, data)
        # notificação 0x39 que a impressora real envia
        request.sendall(bytes([0x1B, 0x1F, 0x00, 0x00, 0x39, 0x00, 0x1C, 0x17]))
        request.sendall(bytes([0x1B, 0x1F, 0x00, 0x00, 0x39, 0x01, 0x1C, 0x17]))
    elif cmd == 0x10:  # clear cache
        registrar("priv", "clear cache (nao apaga codigo do scanner)")
        resp(0x10)
    elif cmd == 0x4D:  # heartbeat
        registrar("priv", "heartbeat")
        resp(0x4D)
    elif cmd == 0x4C:  # query device name
        nome = b"SS5632"
        registrar("priv", "query device name")
        resp(0x4C, nome)
    elif cmd == 0x3F:  # query counter
        slot = mode
        if slot == 0:
            valor = str(contador).encode()
        else:
            valor = b"0"
        registrar("priv", f"query counter slot={slot}")
        resp(0x3F, valor, slot)
    elif cmd == 0x4F:  # reset counter
        global contador
        if mode == 0:
            contador = 0
            registrar("priv", "reset product counter")
        resp(0x4F)
    elif cmd == 0x07:  # enable/disable callback
        callback_hab = (mode == 0x01)
        registrar("priv", f"callback impressao {'ON' if callback_hab else 'off'}")
        # não tem resposta documentada
    elif cmd == 0x29:  # ink status
        registrar("priv", "ink status query")
        resp(0x20 if jet_on else 0x21, mode=0x01)
    elif cmd == 0x20:  # jet on
        jet_on = True
        registrar("priv", "jet on")
        resp(0x20, mode=0x01)
    elif cmd == 0x21:  # jet off
        jet_on = False
        registrar("priv", "jet off")
        resp(0x21, mode=0x01)
    elif cmd == 0x46:  # query alarm
        algum = any(discrete.get(i, 0) for i in range(2, 34))
        if algum:
            bits = "".join(str(discrete.get(i, 0)) for i in range(2, 34))
            registrar("priv", "query alarm - alarmes ativos")
            resp(0x46, bits.encode(), 0x01)
        else:
            registrar("priv", "query alarm - sem alarmes")
            resp(0x46)
    else:
        registrar("priv", f"cmd desconhecido 0x{cmd:02X} - echo")
        resp(cmd, data, mode)


class PrivHandler(socketserver.BaseRequestHandler):
    def handle(self):
        peer = self.client_address[0]
        registrar("sys", f"cliente conectou (4008) {peer}")
        try:
            buf = b""
            while True:
                b = self.request.recv(256)
                if not b:
                    break
                buf += b
                # envia callbacks pendentes
                while callbacks_pendentes:
                    self.request.sendall(callbacks_pendentes.pop(0))
                while b"\x1c\x17" in buf:
                    fim = buf.index(b"\x1c\x17") + 2
                    pkt = buf[:fim]
                    buf = buf[fim:]
                    processar_privado(pkt, self.request)
        except OSError:
            pass
        finally:
            registrar("sys", f"cliente desconectou (4008) {peer}")


class ThreadedServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


# ================================================================
#                       Interface gráfica
# ================================================================
def _ip_local():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return "127.0.0.1"


class App:
    def __init__(self, root):
        self.root = root
        root.title(f"Impressora Falsa SS5632 — Simulador v{APP_VERSION}")
        root.geometry("1000x720")
        root.configure(bg=C_BG)
        root.minsize(900, 640)

        # ============ fontes
        self.f_h1 = font.Font(family="Segoe UI", size=15, weight="bold")
        self.f_h2 = font.Font(family="Segoe UI", size=11, weight="bold")
        self.f_md = font.Font(family="Segoe UI", size=10)
        self.f_sm = font.Font(family="Segoe UI", size=9)
        self.f_mono = font.Font(family="Consolas", size=10)
        self.f_mono_sm = font.Font(family="Consolas", size=9)
        self.f_grande = font.Font(family="Segoe UI", size=20, weight="bold")

        ip = _ip_local()

        # ============ barra topo
        topo = tk.Frame(root, bg=C_PANEL, height=68)
        topo.pack(fill="x")
        topo.pack_propagate(False)
        tk.Label(topo, text="🖨  IMPRESSORA FALSA  —  Codeline SS5632",
                 bg=C_PANEL, fg=C_TXT, font=self.f_h1).pack(side="left", padx=18, pady=6)
        info = tk.Frame(topo, bg=C_PANEL)
        info.pack(side="right", padx=18)
        tk.Label(info, text=f"Modbus TCP {ip}:{MODBUS_PORT}",
                 bg=C_PANEL, fg=C_MODBUS, font=self.f_md).pack(anchor="e")
        tk.Label(info, text=f"Privado {ip}:{PRIV_PORT}",
                 bg=C_PANEL, fg=C_PRIV, font=self.f_md).pack(anchor="e")

        # ============ abas
        style = ttk.Style()
        style.theme_use("default")
        style.configure("TNotebook", background=C_BG, borderwidth=0)
        style.configure("TNotebook.Tab", background=C_PANEL, foreground=C_DIM,
                        padding=[18, 8], font=self.f_md, borderwidth=0)
        style.map("TNotebook.Tab",
                  background=[("selected", C_BG)],
                  foreground=[("selected", C_TXT)])

        nb = ttk.Notebook(root)
        nb.pack(fill="both", expand=True, padx=12, pady=(10, 8))
        self.aba_painel(nb)
        self.aba_registros(nb)
        self.aba_log(nb)
        self.aba_sobre(nb)

        # ============ rodapé
        rod = tk.Frame(root, bg=C_PANEL, height=28)
        rod.pack(fill="x")
        rod.pack_propagate(False)
        tk.Label(rod, text=f"  Autor: {AUTHOR}", bg=C_PANEL, fg=C_DIM,
                 font=self.f_sm).pack(side="left")
        tk.Label(rod, text=REPO + "   ", bg=C_PANEL, fg=C_ACCENT,
                 font=self.f_sm, cursor="hand2").pack(side="right")

        self._ult_log = -1
        self.tick()

    def aba_painel(self, nb):
        f = tk.Frame(nb, bg=C_BG)
        nb.add(f, text="Painel")

        esq = tk.Frame(f, bg=C_BG)
        esq.pack(side="left", fill="both", expand=True, padx=(12, 6), pady=12)
        dir_ = tk.Frame(f, bg=C_BG)
        dir_.pack(side="right", fill="y", padx=(6, 12), pady=12)

        # ---- entrada do scanner
        self._card(esq, "LEITOR DE CÓDIGO DE BARRAS",
                   "mire aqui ou digite o código e aperte Enter")
        self.entrada = tk.Entry(esq, font=self.f_grande, bg=C_PANEL2, fg=C_TXT,
                                relief="flat", insertbackground=C_TXT, bd=0)
        self.entrada.pack(fill="x", padx=12, pady=(0, 12), ipady=12)
        self.entrada.bind("<Return>", self.on_scan)
        self.entrada.focus_set()

        # ---- código atual
        self._card(esq, "CÓDIGO ATUAL NO TEMPLATE (4x0201)",
                   "escrito pelo leitor USB")
        self.lbl_codigo = tk.Label(esq, text="(vazio)", font=self.f_grande,
                                   bg=C_PANEL2, fg=C_DIM, pady=14)
        self.lbl_codigo.pack(fill="x", padx=12, pady=(0, 12))

        # ---- estado + botões
        self._card(esq, "AÇÕES SIMULADAS", "")
        btns = tk.Frame(esq, bg=C_BG)
        btns.pack(fill="x", padx=12, pady=(0, 12))
        self._botao(btns, "▶  Imprimir produto", imprimir_fake, C_OK).pack(side="left", padx=(0, 6))
        self._botao(btns, "✖  Apagar código", lambda: set_codigo(""), C_WARN).pack(side="left", padx=6)
        self._botao(btns, "⚠  Alarme ON", lambda: set_alarme(2, True), C_ERR).pack(side="left", padx=6)
        self._botao(btns, "✓  Alarme OFF", lambda: set_alarme(2, False), C_DIM).pack(side="left", padx=6)

        # ---- stats na direita
        self._card(dir_, "ESTADO", "", w=260)
        self.lbl_contador = self._stat(dir_, "Contador 4x0061", "0")
        self.lbl_cache = self._stat(dir_, "Cache 3x0002", "0")
        self.lbl_jet = self._stat(dir_, "Jet", "ON")
        self.lbl_ready = self._stat(dir_, "Printer ready", "sim")
        self.lbl_alarme = self._stat(dir_, "Alarme (1x0001)", "não")
        self.lbl_template = self._stat(dir_, "Template", template_atual)

    def aba_registros(self, nb):
        f = tk.Frame(nb, bg=C_BG)
        nb.add(f, text="Registros Modbus")

        tk.Label(f, text="valores em tempo real dos registros Modbus",
                 bg=C_BG, fg=C_DIM, font=self.f_sm).pack(anchor="w", padx=16, pady=(8, 4))

        cols = ("end", "nome", "valor")
        tree = ttk.Treeview(f, columns=cols, show="headings", height=20)
        tree.heading("end", text="Endereço")
        tree.heading("nome", text="Descrição")
        tree.heading("valor", text="Valor atual")
        tree.column("end", width=110)
        tree.column("nome", width=440)
        tree.column("valor", width=360)

        style = ttk.Style()
        style.configure("Treeview",
                        background=C_PANEL, foreground=C_TXT, fieldbackground=C_PANEL,
                        rowheight=22, borderwidth=0, font=self.f_mono_sm)
        style.configure("Treeview.Heading",
                        background=C_PANEL2, foreground=C_TXT,
                        font=self.f_h2, relief="flat")
        style.map("Treeview", background=[("selected", C_HIGHLIGHT)])

        tree.pack(fill="both", expand=True, padx=16, pady=(0, 16))
        self.tree = tree

        self._regs_mostrados = [
            ("3x0002", "cache quantity", lambda: input_regs.get(1, 0)),
            ("3x0021", "machine state", lambda: input_regs.get(20, 0)),
            ("3x0004", "% tinta restante", lambda: input_regs.get(3, 0)),
            ("3x0005", "% solvente restante", lambda: input_regs.get(4, 0)),
            ("3x0006", "viscosidade", lambda: input_regs.get(5, 0)),
            ("3x0014", "temp tinta", lambda: input_regs.get(13, 0)),
            ("3x0017", "temp ambiente", lambda: input_regs.get(16, 0)),
            ("1x0001", "flag alarme (algum ativo)", lambda: discrete.get(0, 0)),
            ("1x0100", "printer ready", lambda: discrete.get(99, 0)),
            ("4x0061", "product counter (Dword)", lambda: contador),
            ("4x0200", "flag tem código?", lambda: f"0x{holding.get(199, 0):04X}"),
            ("4x0201..", "código do scanner (ASCII)", ler_codigo_atual),
            ("0x0002", "clear cache", lambda: coils.get(1, 0)),
            ("0x0003", "cache mode enable", lambda: coils.get(2, 0)),
            ("0x0019", "SN enable", lambda: coils.get(24, 0)),
        ]
        for end, nome, _ in self._regs_mostrados:
            tree.insert("", "end", iid=end, values=(end, nome, "..."))

    def aba_log(self, nb):
        f = tk.Frame(nb, bg=C_BG)
        nb.add(f, text="Atividade")

        tk.Label(f, text="log de todo o tráfego recebido e enviado",
                 bg=C_BG, fg=C_DIM, font=self.f_sm).pack(anchor="w", padx=16, pady=(8, 4))

        caixa = tk.Frame(f, bg=C_PANEL)
        caixa.pack(fill="both", expand=True, padx=16, pady=(0, 16))
        self.log = tk.Text(caixa, font=self.f_mono, bg=C_PANEL, fg=C_TXT,
                           relief="flat", bd=0, padx=12, pady=8, state="disabled",
                           insertbackground=C_TXT)
        self.log.pack(side="left", fill="both", expand=True)
        sb = ttk.Scrollbar(caixa, command=self.log.yview)
        sb.pack(side="right", fill="y")
        self.log.configure(yscrollcommand=sb.set)

        self.log.tag_config("hora", foreground=C_DIM)
        self.log.tag_config("mb", foreground=C_MODBUS)
        self.log.tag_config("priv", foreground=C_PRIV)
        self.log.tag_config("sys", foreground=C_SYS)

    def aba_sobre(self, nb):
        f = tk.Frame(nb, bg=C_BG)
        nb.add(f, text="Sobre")

        box = tk.Frame(f, bg=C_PANEL)
        box.pack(fill="x", padx=24, pady=24)

        tk.Label(box, text=f"Simulador da impressora Codeline SS5632",
                 bg=C_PANEL, fg=C_TXT, font=self.f_h1).pack(anchor="w", padx=20, pady=(20, 4))
        tk.Label(box, text=f"versão {APP_VERSION}",
                 bg=C_PANEL, fg=C_DIM, font=self.f_sm).pack(anchor="w", padx=20)
        tk.Label(box, text="\nFaz Modbus TCP (1502) + protocolo privado (4008)\n"
                           "para testar o StamPLC sem a impressora física.\n\n"
                           "Reproduz os comportamentos reais que descobrimos:\n"
                           " • Código do scanner no holding 4x0201 (não no 3x0201)\n"
                           " • Flag 0xFFFF em 4x0200 quando tem código\n"
                           " • Comando 0x33 na porta 4008 apaga o código\n"
                           " • Contador 4x0061 só sobe com impressão real\n"
                           " • Coils 0x0003 e 0x0019 que o StamPLC habilita\n",
                 bg=C_PANEL, fg=C_TXT, font=self.f_md, justify="left").pack(anchor="w", padx=20)
        tk.Label(box, text=f"Autor: {AUTHOR}",
                 bg=C_PANEL, fg=C_TXT, font=self.f_h2).pack(anchor="w", padx=20, pady=(10, 0))
        tk.Label(box, text=REPO,
                 bg=C_PANEL, fg=C_ACCENT, font=self.f_md).pack(anchor="w", padx=20, pady=(0, 20))

    def _card(self, parent, titulo, subtitulo, w=None):
        """Adiciona um pequeno cabeçalho de cartão."""
        tk.Label(parent, text=titulo, bg=C_BG, fg=C_DIM,
                 font=self.f_sm).pack(anchor="w", padx=14, pady=(0, 2))
        if subtitulo:
            tk.Label(parent, text=subtitulo, bg=C_BG, fg=C_DIM,
                     font=self.f_sm).pack(anchor="w", padx=14, pady=(0, 4))

    def _botao(self, parent, texto, cmd, cor):
        b = tk.Button(parent, text=texto, command=cmd, font=self.f_md,
                      bg=C_PANEL2, fg=cor, activebackground=C_HIGHLIGHT,
                      activeforeground=C_TXT, relief="flat", bd=0,
                      padx=14, pady=8, cursor="hand2")
        return b

    def _stat(self, parent, nome, valor):
        f = tk.Frame(parent, bg=C_PANEL2, pady=8, padx=14)
        f.pack(fill="x", padx=14, pady=4)
        tk.Label(f, text=nome.upper(), bg=C_PANEL2, fg=C_DIM,
                 font=self.f_sm).pack(anchor="w")
        lbl = tk.Label(f, text=valor, bg=C_PANEL2, fg=C_TXT,
                       font=self.f_h2)
        lbl.pack(anchor="w")
        return lbl

    def on_scan(self, _ev=None):
        c = self.entrada.get().strip()
        self.entrada.delete(0, "end")
        if c:
            set_codigo(c)

    def tick(self):
        # código atual
        atual = ler_codigo_atual()
        if atual:
            self.lbl_codigo.configure(text=atual, fg=C_OK)
        else:
            self.lbl_codigo.configure(text="(vazio - aguardando leitura)", fg=C_DIM)

        # estado
        self.lbl_contador.configure(text=str(contador))
        self.lbl_cache.configure(text=str(input_regs.get(1, 0)))
        self.lbl_jet.configure(text="ON" if jet_on else "OFF",
                               fg=C_OK if jet_on else C_WARN)
        self.lbl_ready.configure(text="sim" if discrete.get(99, 0) else "não")
        self.lbl_alarme.configure(text="SIM" if discrete.get(0, 0) else "não",
                                  fg=C_ERR if discrete.get(0, 0) else C_DIM)
        self.lbl_template.configure(text=template_atual)

        # registros
        for end, _, fn in self._regs_mostrados:
            try:
                v = fn()
                self.tree.set(end, "valor", str(v) if not isinstance(v, int)
                              or end.startswith("1x") or end.startswith("0x")
                              else f"{v}")
            except Exception:
                pass

        # log
        if len(historico) - 1 != self._ult_log:
            self.log.configure(state="normal")
            for i in range(self._ult_log + 1, len(historico)):
                h, cat, msg = historico[i]
                self.log.insert("end", f"[{h}] ", "hora")
                self.log.insert("end", f"{cat:4} ", cat)
                self.log.insert("end", f"{msg}\n")
            self.log.see("end")
            self.log.configure(state="disabled")
            self._ult_log = len(historico) - 1

        self.entrada.focus_set()
        self.root.after(200, self.tick)


# ================================================================
#                       Inicialização
# ================================================================
set_codigo("")

srv_mb = ThreadedServer(("0.0.0.0", MODBUS_PORT), ModbusHandler)
srv_priv = ThreadedServer(("0.0.0.0", PRIV_PORT), PrivHandler)
threading.Thread(target=srv_mb.serve_forever, daemon=True).start()
threading.Thread(target=srv_priv.serve_forever, daemon=True).start()
registrar("sys", f"Modbus TCP escutando na :{MODBUS_PORT}")
registrar("sys", f"Protocolo privado escutando na :{PRIV_PORT}")

root = tk.Tk()
app = App(root)
try:
    root.mainloop()
finally:
    srv_mb.shutdown()
    srv_priv.shutdown()
