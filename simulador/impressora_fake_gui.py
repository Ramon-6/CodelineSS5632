"""
Simulador da impressora com janela grafica, para testar o StamPLC em bancada.

- Abre um servidor Modbus TCP na porta 1502 (igual a impressora de verdade).
- Mostra uma janela com um campo que recebe a leitura do leitor de codigo de
  barras (que entra como teclado USB: digita o codigo + Enter).
- Quando um codigo e lido, atualiza os registradores Modbus que o StamPLC
  monitora (3x0201 texto, 3x0002 cache) para disparar o ciclo.
- Quando o StamPLC manda apagar (bobina 0x0002 = 1), o codigo some.

Uso:
  python impressora_fake_gui.py
  Depois configure o StamPLC em Ajustes com o IP deste PC.
  Mire o leitor na janela; o campo "codigo" captura.

Sem dependencias extras alem do Python padrao (tkinter + socketserver).
"""
import socket
import socketserver
import struct
import threading
import time
import tkinter as tk
from tkinter import ttk, font

HOST, PORT = "0.0.0.0", 1502

# Estado compartilhado da impressora
estado_lock = threading.Lock()
input_regs = {}   # fc04: enderecos base zero
coils = {}        # bobinas (fc01/05)
contador = 0
historico = []    # lista de (hora, evento, detalhe)


def _ip_local():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return "127.0.0.1"


def registrar(evento, detalhe=""):
    historico.append((time.strftime("%H:%M:%S"), evento, detalhe))
    if len(historico) > 200:
        del historico[:50]


def set_codigo(txt):
    global contador
    with estado_lock:
        for i in range(49):
            input_regs[200 + i] = ord(txt[i]) if i < len(txt) else 0
        input_regs[1] = 1 if txt else 0  # 3x0002 cache
        if txt:
            contador += 1
        registrar("CODIGO RECEBIDO" if txt else "cache limpo", txt)


def ler_codigo_atual():
    with estado_lock:
        return "".join(chr(input_regs.get(200 + i, 0)) for i in range(49) if input_regs.get(200 + i, 0))


def pdu(fc, data):
    global contador
    with estado_lock:
        if fc in (3, 4):
            a, q = struct.unpack(">HH", data[:4])
            src = input_regs if fc == 4 else {60: contador >> 16, 61: contador & 0xFFFF}
            vals = [src.get(a + i, 0) for i in range(q)]
            return bytes([fc, q * 2]) + struct.pack(">" + "H" * q, *vals)
        if fc in (1, 2):
            a, q = struct.unpack(">HH", data[:4])
            v = coils.get(a, 0) if fc == 1 else (1 if a == 99 else 0)
            return bytes([fc, 1, v])
        if fc == 5:
            a, v = struct.unpack(">HH", data[:4])
            coils[a] = 1 if v == 0xFF00 else 0
            if a == 1 and coils[a]:  # StamPLC mandou limpar o cache
                registrar("STAMPLC pediu apagar", "")
                for i in range(49):
                    input_regs[200 + i] = 0
                input_regs[1] = 0
                coils[1] = 0  # "zera quando termina"
            return bytes([fc]) + data[:4]
    return bytes([fc | 0x80, 1])


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        peer = self.client_address[0]
        registrar("StamPLC conectou", peer)
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
            registrar("StamPLC desconectou", peer)


class ThreadedServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


# ======================= Interface grafica =======================
class App:
    def __init__(self, root):
        root.title("Impressora Falsa - Simulador para StamPLC")
        root.geometry("640x560")
        root.configure(bg="#f6f7fa")

        ip = _ip_local()
        fonte_mono = font.Font(family="Consolas", size=11)
        fonte_grande = font.Font(family="Segoe UI", size=22, weight="bold")
        fonte_md = font.Font(family="Segoe UI", size=11)

        topo = tk.Frame(root, bg="#1f2937", pady=14)
        topo.pack(fill="x")
        tk.Label(topo, text="IMPRESSORA FALSA", bg="#1f2937", fg="white",
                 font=("Segoe UI", 15, "bold")).pack()
        tk.Label(topo, text=f"Modbus TCP: {ip}:{PORT}  (configure no StamPLC)",
                 bg="#1f2937", fg="#cbd5e1", font=fonte_md).pack(pady=(2, 0))

        corpo = tk.Frame(root, bg="#f6f7fa", padx=18, pady=14)
        corpo.pack(fill="both", expand=True)

        tk.Label(corpo, text="MIRE O LEITOR NA CAIXA ABAIXO:", bg="#f6f7fa",
                 fg="#444", font=("Segoe UI", 10, "bold")).pack(anchor="w")
        self.entrada = tk.Entry(corpo, font=fonte_grande, bg="white", relief="solid", bd=2)
        self.entrada.pack(fill="x", pady=(4, 10), ipady=10)
        self.entrada.bind("<Return>", self.ler_codigo)
        self.entrada.focus_set()

        self.lbl_atual_titulo = tk.Label(corpo, text="CODIGO ATUAL NA IMPRESSORA",
                                         bg="#f6f7fa", fg="#444", font=("Segoe UI", 10, "bold"))
        self.lbl_atual_titulo.pack(anchor="w", pady=(10, 2))
        self.lbl_atual = tk.Label(corpo, text="(vazio)", bg="white", fg="#1c202a",
                                  font=fonte_grande, relief="solid", bd=1, pady=10)
        self.lbl_atual.pack(fill="x")

        linha_botoes = tk.Frame(corpo, bg="#f6f7fa")
        linha_botoes.pack(fill="x", pady=(10, 4))
        tk.Button(linha_botoes, text="Limpar codigo (manual)", command=lambda: set_codigo(""),
                  font=fonte_md, bg="#eff2f6").pack(side="left")
        tk.Label(linha_botoes, text="  ou escaneie com o leitor", bg="#f6f7fa",
                 fg="#666", font=("Segoe UI", 9, "italic")).pack(side="left")

        tk.Label(corpo, text="HISTORICO (ultimos eventos)", bg="#f6f7fa",
                 fg="#444", font=("Segoe UI", 10, "bold")).pack(anchor="w", pady=(14, 2))
        caixa_log = tk.Frame(corpo)
        caixa_log.pack(fill="both", expand=True)
        self.txt = tk.Text(caixa_log, font=fonte_mono, bg="white", relief="solid", bd=1, height=10)
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
        # atualiza codigo atual
        atual = ler_codigo_atual()
        if atual:
            self.lbl_atual.configure(text=atual, bg="#dcf3e3", fg="#108844")
        else:
            self.lbl_atual.configure(text="(vazio - aguardando leitura)", bg="white", fg="#9aa0ab")
        # historico incremental
        if len(historico) - 1 != self._ult_hist:
            self.txt.configure(state="normal")
            for i in range(self._ult_hist + 1, len(historico)):
                h, ev, det = historico[i]
                self.txt.insert("end", f"[{h}] {ev}" + (f"  {det}" if det else "") + "\n")
            self.txt.see("end")
            self.txt.configure(state="disabled")
            self._ult_hist = len(historico) - 1
        self.entrada.focus_set()  # mantem foco pro leitor digitar sempre aqui
        root.after(150, self.tick)


# Inicia servidor Modbus e janela
set_codigo("")
srv = ThreadedServer((HOST, PORT), Handler)
threading.Thread(target=srv.serve_forever, daemon=True).start()
registrar("Servidor Modbus iniciado", f"porta {PORT}")

root = tk.Tk()
app = App(root)
root.mainloop()
srv.shutdown()
