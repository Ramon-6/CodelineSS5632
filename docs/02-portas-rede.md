# Portas de rede

A impressora Codeline SS5632 expõe vários serviços na rede. **O que o
manual mostra e o que acontece de verdade são coisas diferentes** —
documento aqui os dois lados.

## Portas abertas (scan feito do PC)

| Porta | Protocolo | Serve para |
|---|---|---|
| 21 | FTP | Transferência de arquivos de template (.hik, .txt, .csv, .bmp) |
| 23 | Telnet | Debug do sistema operacional interno (Linux) |
| **502** | Modbus TCP | **Aberta mas NÃO responde Modbus** — só a 1502 funciona |
| **1502** | Modbus TCP | **Modbus real** — todas as operações de leitura/escrita |
| 4002 | Privado | Reserva não documentada |
| 4008 | Privado proprietário | Controle total: impressão, template, status, callback |
| **4040** | Privado interno | **Comunicação interna do leitor USB** — só aceita cliente localhost |
| 8017 | Privado | Reserva não documentada |

### Observações importantes

1. **Porta 502 vs 1502**: a 502 é o padrão oficial do Modbus TCP, mas
   nessa impressora o serviço real está na **1502**. A 502 abre o socket
   mas não responde comandos Modbus. **Use 1502**.

2. **Porta 4040 é intocável externamente**: tentei conectar como cliente
   do PLC e sempre fui rejeitado (ECONNREFUSED). O log da impressora
   mostra que ela só aceita conexão de `127.0.0.1` (processo interno
   dela mesma, que lida com o leitor USB).

3. **Serial RS232 e Ethernet são mutuamente exclusivas**: na tela
   "Serial Port Setting" se o campo "Protocolo" for qualquer coisa
   além de **None**, o Ethernet Modbus é desabilitado — o ping continua
   funcionando mas a porta 1502 fica fechada. Durante nosso projeto
   isso aconteceu uma vez por engano e nos custou uma hora de debug.

4. **A porta 4008 aceita múltiplas conexões**: diferente da 4040, dá
   para ter o PLC conectado permanentemente. Mas se estiver fazendo
   muitas conexões/desconexões rápidas, pode esgotar.

5. **A tela "Visualizar dados de comunicação"** da impressora mostra
   tudo que trafega nessas portas em tempo real. Útil para debug.

## Como habilitar/desabilitar

### Modbus TCP (1502)

Menu principal da impressora: deve estar ativo por padrão. Se tiver
sido desligado, procurar em "Device Debug → Protocol Setting" (menu
de engenharia que pode exigir senha do técnico Codeline).

### Protocolo serial (RS232 nos pinos 3/6 do conector Alarm)

Tela "Serial Port Setting":
- Taxa 9600, 8 bits, Sem paridade, 1 stop, sem controle de fluxo
- **Protocolo**: None (desabilita) ou Modbus RTU, 8017, 4008, 4002, 4040

Habilitar qualquer protocolo serial **desabilita a Ethernet**.

### Encaminhamento (aba Clinet da tela Visualizar dados)

Permite que a impressora conecte em outro IP como cliente para
retransmitir dados. **Não conseguimos fazer funcionar** — configuramos
o IP do PLC e Server Port, mas a impressora continuou falando apenas
com o 127.0.0.1 interno. Pode exigir configuração que não é visível.
