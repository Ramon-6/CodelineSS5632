"""Impressora falsa (Modbus TCP, porta 1502) para testar o StamPLC sem a impressora real.

Uso:  python impressora_fake.py
Digite um codigo e Enter para simular o operador lendo o leitor de codigo de barras.
Quando o StamPLC limpar o cache (bobina 0x0002), o codigo some, como na impressora.
"""
import socketserver
import struct
import threading

HOST, PORT = "0.0.0.0", 1502
lock = threading.Lock()
input_regs = {}   # fc04: endereco base zero
coils = {}        # fc01/05
contador = 0


def set_codigo(txt):
    with lock:
        for i in range(49):
            input_regs[200 + i] = ord(txt[i]) if i < len(txt) else 0
        input_regs[1] = 1 if txt else 0   # 3x0002 cache


def pdu(fc, data):
    global contador
    with lock:
        if fc in (3, 4):
            a, q = struct.unpack(">HH", data[:4])
            src = input_regs if fc == 4 else {60: contador >> 16, 61: contador & 0xFFFF}
            vals = [src.get(a + i, 0) for i in range(q)]
            return bytes([fc, q * 2]) + struct.pack(">" + "H" * q, *vals)
        if fc in (1, 2):
            a, q = struct.unpack(">HH", data[:4])
            v = coils.get(a, 0) if fc == 1 else (1 if a == 99 else 0)  # 1x0100 pronta
            return bytes([fc, 1, v])
        if fc == 5:
            a, v = struct.unpack(">HH", data[:4])
            coils[a] = 1 if v == 0xFF00 else 0
            if a == 1 and coils[a]:       # limpar cache
                print("  [impressora] cache limpo pelo StamPLC")
                for i in range(49):
                    input_regs[200 + i] = 0
                input_regs[1] = 0
                coils[1] = 0              # "volta a 0 quando termina"
            return bytes([fc]) + data[:4]
    return bytes([fc | 0x80, 1])


class H(socketserver.BaseRequestHandler):
    def handle(self):
        while True:
            h = self.request.recv(7)
            if len(h) < 7:
                return
            tid, _, ln, unit = struct.unpack(">HHHB", h)
            body = self.request.recv(ln - 1)
            r = pdu(body[0], body[1:])
            self.request.sendall(struct.pack(">HHHB", tid, 0, len(r) + 1, unit) + r)


class S(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


set_codigo("")
threading.Thread(target=S((HOST, PORT), H).serve_forever, daemon=True).start()
print(f"Impressora falsa na porta {PORT}. Digite um codigo e Enter para simular a leitura (Ctrl+C sai).")
while True:
    c = input("codigo> ").strip()
    if c:
        contador += 1
        set_codigo(c)
        print("  [impressora] recebeu", c)
