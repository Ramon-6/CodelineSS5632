"""
Simulador da impressora Codeline SS5632 para testar o StamPLC em bancada.

Reproduz o comportamento REAL que descobrimos na fabrica:
  - Modbus TCP na porta 1502
  - O codigo lido pelo leitor USB vai no HOLDING register 4x0201 (fc=03, addr 200+)
  - Flag 4x0200 (addr 199) vai pra 0xFFFF quando tem codigo
  - Terminador 0x09 no fim do codigo
  - Contador 4x0061 (addr 60-61 Dword) incrementa a cada "impressao" (botao Imprimir)
  - Comando 0x33 na porta 4008 (reload template) APAGA o codigo

Uso:
  python impressora_fake_gui.py
  Depois configure o StamPLC em Ajustes com o IP deste PC (ex: 192.168.1.40).
  Mire o leitor na janela ou digite codigos no campo.
"""
import socket
import socketserver
import struct
import threading
import time
import tkinter as tk
from tkinter import font

MODBUS_PORT, PRIV_PORT = 1502, 4008
TEMPLATE_NOME = "sumi rico"

lock = threading.Lock()
holding = {}      # 4xXXXX - fc=03/06/16 (onde o LEITOR escreve o codigo)
input_regs = {}   # 3xXXXX - fc=04
coils = {}        # 0xXXXX - fc=01/05
contador = 0      # total de "impressoes"
historico = []


def registrar(ev, det=""):
    historico.append((time.strftime("%H:%M:%S"), ev, det))
    if len(historico) > 200:
        del historico[:50]


def _ip_local():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return "127.0.0.1"


def set_codigo(txt):
    """Simula o leitor USB preenchendo o template da impressora."""
    with lock:
        # flag em 4x0200 (addr 199): 0xFFFF se tem codigo, 0 se vazio
        holding[199] = 0xFFFF if txt else 0
        # codigo em 4x0201..0213 (addr 200 em diante) + terminador 0x09
        for i in range(20):
            if i < len(txt):
                holding[200 + i] = ord(txt[i])
            elif i == len(txt) and txt:
                holding[200 + i] = 0x09  # terminador
            else:
                holding[200 + i] = 0
        # input register 3x0002 (cache) tambem indica
        input_regs[1] = 1 if txt else 0
        registrar("CODIGO LIDO" if txt else "codigo apagado", txt)


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
    """Simula uma impressao (como se produto passasse pela fotocelula)."""
    global contador
    with lock:
        contador += 1
        registrar("IMPRIMIU", f"contador={contador}")


def pdu(fc, data):
    """Processa um PDU Modbus e devolve a resposta."""
    global contador
    with lock:
        if fc == 3 or fc == 4:  # read holding/input
            a, q = struct.unpack(">HH", data[:4])
            if fc == 3:
                src = holding
                # 4x0061 - 4x0062 e o contador 32-bit (Long AB CD)
                src[60] = (contador >> 16) & 0xFFFF
                src[61] = contador & 0xFFFF
            else:
                src = input_regs
            vals = [src.get(a + i, 0) for i in range(q)]
            return bytes([fc, q * 2]) + struct.pack(">" + "H" * q, *vals)
        if fc == 1 or fc == 2:
            a, q = struct.unpack(">HH", data[:4])
            v = coils.get(a, 0) if fc == 1 else (1 if a == 99 else 0)  # 1x0100 = pronta
            return bytes([fc, 1, v])
        if fc == 5:  # write single coil
            a, v = struct.unpack(">HH", data[:4])
            coils[a] = 1 if v == 0xFF00 else 0
            if a == 1 and coils[a]:
                # bobina 0x0002 (coil addr 1) - clear network cache (nao apaga codigo do leitor)
                registrar("StamPLC: clear cache", "")
                coils[1] = 0  # auto-reseta
            if a == 2 and coils[a]:
                registrar("StamPLC: cache mode ON", "")
            if a == 24 and coils[a]:
                registrar("StamPLC: SN enable ON", "")
            return bytes([fc]) + data[:4]
        if fc == 6:  # write single holding
            a, v = struct.unpack(">HH", data[:4])
            holding[a] = v
            return bytes([fc]) + data[:4]
        if fc == 16:  # write multiple holding
            a, q = struct.unpack(">HH", data[:4])
            n = data[4]
            for i in range(q):
                holding[a + i] = (data[5 + i * 2] << 8) | data[6 + i * 2]
            return bytes([fc]) + data[:4]
    return bytes([fc | 0x80, 1])


class ModbusHandler(socketserver.BaseRequestHandler):
    def handle(self):
        peer = self.client_address[0]
        registrar("StamPLC conectou", f"Modbus {peer}")
        try:
            while True:
                h = self.request.recv(7)
                if len(h) < 7:
                    return
                tid, _, ln, unit = struct.unpack(">HHHB", h)
                body = self.request.recv(ln - 1)
                r = pdu(body[0], body[1:])
                self.request.sendall(struct.pack(">HHHB", tid, 0, len(r) + 1, unit) + r)
        except OSError:
            pass
        finally:
            registrar("StamPLC desconectou", f"Modbus {peer}")


class PrivHandler(socketserver.BaseRequestHandler):
    """Protocolo privado da impressora - porta 4008.
    Formato: 1B 1F [len_lo] [len_hi] [cmd] [data] [mode] 1C 17
    """

    def handle(self):
        peer = self.client_address[0]
        registrar("StamPLC conectou", f"4008 {peer}")
        try:
            buf = b""
            while True:
                b = self.request.recv(128)
                if not b:
                    break
                buf += b
                # processa pacotes completos
                while b"\x1c\x17" in buf:
                    fim = buf.index(b"\x1c\x17") + 2
                    pkt = buf[:fim]
                    buf = buf[fim:]
                    if len(pkt) >= 8 and pkt[0] == 0x1B and pkt[1] == 0x1F:
                        cmd = pkt[4]
                        if cmd == 0x33 or cmd == 0x43:
                            # reload / open template - apaga o codigo
                            registrar("StamPLC: reload template", f"cmd=0x{cmd:02X}")
                            set_codigo("")
                            self.request.sendall(pkt)
                        elif cmd == 0x4D:
                            # heartbeat
                            self.request.sendall(pkt)
                        elif cmd == 0x10:
                            # clear cache
                            registrar("StamPLC: clear cache 4008", "")
                            self.request.sendall(pkt)
                        else:
                            # echo generico
                            self.request.sendall(pkt)
        except OSError:
            pass
        finally:
            registrar("StamPLC desconectou", f"4008 {peer}")


class ThreadedServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


# ==================== GUI ====================
class App:
    def __init__(self, root):
        root.title("Impressora Falsa - Simulador Codeline SS5632")
        root.geometry("680x640")
        root.configure(bg="#f6f7fa")

        ip = _ip_local()
        f_grande = font.Font(family="Segoe UI", size=22, weight="bold")
        f_md = font.Font(family="Segoe UI", size=11)
        f_mono = font.Font(family="Consolas", size=11)

        topo = tk.Frame(root, bg="#1f2937", pady=12)
        topo.pack(fill="x")
        tk.Label(topo, text="IMPRESSORA FALSA (Codeline SS5632)", bg="#1f2937",
                 fg="white", font=("Segoe UI", 15, "bold")).pack()
        tk.Label(topo, text=f"Modbus TCP {ip}:{MODBUS_PORT}  |  Privado {ip}:{PRIV_PORT}",
                 bg="#1f2937", fg="#cbd5e1", font=f_md).pack(pady=(2, 0))
        tk.Label(topo, text=f"Template atual: {TEMPLATE_NOME}", bg="#1f2937",
                 fg="#94a3b8", font=("Segoe UI", 10)).pack()

        corpo = tk.Frame(root, bg="#f6f7fa", padx=18, pady=14)
        corpo.pack(fill="both", expand=True)

        tk.Label(corpo, text="MIRE O LEITOR OU DIGITE AQUI:", bg="#f6f7fa",
                 fg="#444", font=("Segoe UI", 10, "bold")).pack(anchor="w")
        self.entrada = tk.Entry(corpo, font=f_grande, bg="white", relief="solid", bd=2)
        self.entrada.pack(fill="x", pady=(4, 10), ipady=10)
        self.entrada.bind("<Return>", self.ler_codigo)
        self.entrada.focus_set()

        tk.Label(corpo, text="CODIGO ATUAL NO TEMPLATE (4x0201)", bg="#f6f7fa",
                 fg="#444", font=("Segoe UI", 10, "bold")).pack(anchor="w", pady=(10, 2))
        self.lbl_atual = tk.Label(corpo, text="(vazio)", bg="white", fg="#1c202a",
                                   font=f_grande, relief="solid", bd=1, pady=10)
        self.lbl_atual.pack(fill="x")

        info = tk.Frame(corpo, bg="#f6f7fa")
        info.pack(fill="x", pady=(10, 4))
        self.lbl_contador = tk.Label(info, text="Contador 4x0061: 0", bg="#f6f7fa",
                                     fg="#555", font=f_md)
        self.lbl_contador.pack(side="left")

        btns = tk.Frame(corpo, bg="#f6f7fa")
        btns.pack(fill="x", pady=6)
        tk.Button(btns, text="Imprimir (contador +1)", command=imprimir_fake,
                  font=f_md, bg="#dcf3e3", fg="#108844", relief="raised", bd=1,
                  padx=10, pady=4).pack(side="left")
        tk.Button(btns, text="Apagar codigo (manual)", command=lambda: set_codigo(""),
                  font=f_md, bg="#eff2f6").pack(side="left", padx=(8, 0))

        tk.Label(corpo, text="HISTORICO", bg="#f6f7fa", fg="#444",
                 font=("Segoe UI", 10, "bold")).pack(anchor="w", pady=(14, 2))
        caixa_log = tk.Frame(corpo)
        caixa_log.pack(fill="both", expand=True)
        self.txt = tk.Text(caixa_log, font=f_mono, bg="white", relief="solid", bd=1, height=10)
        self.txt.pack(side="left", fill="both", expand=True)
        self.txt.configure(state="disabled")
        sb = tk.Scrollbar(caixa_log, command=self.txt.yview)
        sb.pack(side="right", fill="y")
        self.txt.configure(yscrollcommand=sb.set)

        self._ult_hist = -1
        self.tick()

    def ler_codigo(self, _ev=None):
        c = self.entrada.get().strip()
        self.entrada.delete(0, "end")
        if c:
            set_codigo(c)

    def tick(self):
        atual = ler_codigo_atual()
        if atual:
            self.lbl_atual.configure(text=atual, bg="#dcf3e3", fg="#108844")
        else:
            self.lbl_atual.configure(text="(vazio - aguardando leitura)",
                                     bg="white", fg="#9aa0ab")
        self.lbl_contador.configure(text=f"Contador 4x0061: {contador}")
        if len(historico) - 1 != self._ult_hist:
            self.txt.configure(state="normal")
            for i in range(self._ult_hist + 1, len(historico)):
                h, ev, det = historico[i]
                linha = f"[{h}] {ev}" + (f"  {det}" if det else "") + "\n"
                self.txt.insert("end", linha)
            self.txt.see("end")
            self.txt.configure(state="disabled")
            self._ult_hist = len(historico) - 1
        self.entrada.focus_set()
        root.after(150, self.tick)


# Inicia os dois servidores
set_codigo("")
srv_mb = ThreadedServer(("0.0.0.0", MODBUS_PORT), ModbusHandler)
srv_priv = ThreadedServer(("0.0.0.0", PRIV_PORT), PrivHandler)
threading.Thread(target=srv_mb.serve_forever, daemon=True).start()
threading.Thread(target=srv_priv.serve_forever, daemon=True).start()
registrar("Servidores iniciados", f"Modbus:{MODBUS_PORT} Priv:{PRIV_PORT}")

root = tk.Tk()
app = App(root)
root.mainloop()
srv_mb.shutdown()
srv_priv.shutdown()
