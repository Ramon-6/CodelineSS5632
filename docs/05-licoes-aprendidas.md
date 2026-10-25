# Lições aprendidas

O que o manual oficial não conta, o que é falso ou enganoso, e os
contornos que descobrimos na prática durante a integração com o
StamPLC.

## Lições sobre a impressora

### 1. O manual diz "3x0201" mas o leitor USB escreve em "4x0201"

O manual Modbus (`modbus - English V.xlsx`) descreve:

> `3x0201` — the next network data to be printed — ASCII characters

Isso sugere que o campo `3x0201` (input register, fc=04) contém o
próximo código a imprimir. Lendo via fc=04, o campo fica sempre vazio
quando o operador usa o leitor USB.

**Realidade**: o scanner escreve no `4x0201` (holding register, fc=03).
É o mesmo endereço do fio mas acessível também para escrita, e é esse
que a impressora preenche quando o scanner é usado.

**Como descobrimos**: fizemos uma varredura comparando ANTES vs DEPOIS
de ler um código, abrangendo input e holding registers. Vimos o
holding mudar; o input ficou zerado.

### 2. O "cache quantity" (3x0002) fica em 0 com o scanner USB

O manual diz que `3x0002` é "cache quantity of inkjet printer" e dá a
entender que é o contador de códigos no buffer. **Fica sempre em 0**
quando o scanner USB é usado — o cache só se preenche quando dados
chegam pela rede Modbus.

### 3. A coil 0x0002 ("Clear network data cache") não apaga o scanner

Para apagar o código do leitor que ficou no template, precisa usar o
comando `0x33` na porta 4008 (force reload template). A coil Modbus
só limpa o cache interno, não o template.

### 4. Comando 0x43 "Open template" não serve para apagar

O manual recomenda 0x43 e desencoraja 0x33. Mas o 0x43 explicitamente
diz "não recarrega se já aberto". Como o template fica sempre aberto
durante a operação, o 0x43 nunca faz nada. **Use 0x33.**

### 5. O contador 4x0061 só sobe com impressão real

É o "Total Prints" visto na tela de Informações do Sistema. Incrementa
apenas quando a impressora realmente imprime (produto passando pela
fotocélula + encoder girando). Não serve como gatilho de leitura do
scanner.

### 6. Habilitar RS232 desabilita Ethernet

Na tela "Serial Port Setting" o campo "Protocolo" tem opções: None,
Test, 8017, Modbus RTU, 4008, 4002, 4040.

Se colocar qualquer coisa diferente de **None**, a Ethernet é
desabilitada. Resultado: ping continua funcionando mas as portas
TCP (incluindo 1502 do Modbus) ficam fechadas.

Durante nosso projeto isso aconteceu uma vez por engano — nos custou
uma hora achando o motivo do PLC não conectar.

### 7. Porta 502 abre mas não responde Modbus

A impressora expõe tanto a 502 (padrão oficial Modbus TCP) quanto a
1502 (customizado). A 502 aceita conexão TCP mas não responde comandos.
**Use sempre a 1502.**

### 8. Porta 4040 não aceita cliente externo

A porta 4040 existe mas é usada internamente pela impressora para
comunicação do processo do scanner USB (sempre com 127.0.0.1). Qualquer
tentativa de conectar externamente recebe ECONNREFUSED. Não é
documentado em lugar nenhum, descobrimos tentando.

### 9. "Clinet" (aba) com IP + Server Port não funciona

A aba "Clinet" da tela "Visualizar dados" promete fazer a impressora
se conectar em um IP+porta externa como cliente, retransmitindo o que
recebe internamente. Configuramos com o IP do PLC e Server Port 4040 —
a impressora continuou falando apenas com 127.0.0.1, nunca tentou
conectar no PLC. Pode ser que precise outra config que só o técnico da
Codeline conhece.

### 10. Impressora às vezes trava e precisa de reset de energia

Após muitas tentativas de conexão (nossos testes do PLC), a impressora
chegou a ficar inacessível (ping também parou). Resolvemos desligando
da tomada por 60 segundos. Reboot via botão de pausa/menu não resolve
nesse caso.

## Lições sobre o StamPLC (M5Stack)

### 11. Socket exhaustion do W5500

O chip W5500 tem **8 sockets no total**. Com servidor HTTP (porta 80) +
outro servidor TCP + cliente Modbus persistente, os sockets podem
esgotar. Sintoma: `mb.connect()` sempre retorna 0 mesmo com rede
perfeita.

Nossa solução: um só servidor TCP (porta 80 para a interface web) e
cliente Modbus persistente. Se precisar de mais um servidor, aumente
o número de sockets em `platformio.ini` ou use cliente local
(instancia-se dentro da função, consome socket só durante uso).

### 12. ESP32 reset não reseta o W5500

`ESP.restart()` reinicia o firmware ESP32 mas o chip W5500 continua
energizado pelo módulo PoE. Qualquer estado zumbi no W5500 (sockets
abertos, cache ARP) persiste. Para reset de verdade: desligar a fonte
12 V por 30 segundos.

### 13. `Ethernet.linkStatus()` pode mentir

Em alguns casos o `linkStatus()` reporta `LinkOFF` mesmo com o cabo
conectado e tráfego funcionando. Usamos apenas como indicador na tela,
não como gate para operações de rede.

### 14. Serial do USB-C desaparece com reset de energia

Ao desligar a fonte 12 V do PLC, o cabo USB-C também perde conexão
(o PLC fica sem energia pro chip USB). A COM reaparece em alguns
segundos após religar.

## Lições sobre pulso de botão industrial

### 15. Pulso de 300 ms não registra

Testamos pulso de 300 ms (duração de um clique humano rápido) para
acionar o botão pulsante da esteira — não registrou. Aumentamos para
**1000 ms** (1 segundo) e funcionou com folga. Pode ser debounce
do botão ou característica do CLP da esteira.

### 16. LED do botão como entrada do PLC é a forma mais confiável

Em vez de tentar ler o status da esteira via comunicação com o CLP da
máquina, usamos o LED que já indica "esteira ligada" como fonte de
sinal 24 V para a entrada do PLC. Dois fios, zero software, 100%
confiável.

## Lições gerais

### 17. Modbus Poll do PC é melhor que firmware no PLC para diagnóstico

Antes de implementar algo no PLC, teste o comportamento da impressora
usando o PC. Script Python de 20 linhas com `socket` resolve
rapidamente. Economiza horas de ciclo de compilar-gravar-ler-serial.

### 18. Tela "Visualizar dados de comunicação" é ouro

A impressora tem uma tela que mostra em tempo real todo o tráfego TCP
que recebe/envia. Foi nela que descobrimos que a 4040 só fala com
127.0.0.1, que a Clinet não enviava para o IP configurado, e que o
comando 0x33 realmente é processado.

Antes de qualquer debug, abra essa tela — ela te poupa muita adivinhação.

### 19. Ping funciona mas Modbus não = suspeite da configuração

Já aconteceu de o ping entre PC e impressora funcionar perfeito mas o
Modbus TCP recusar conexão. **Nunca é problema de cabo nessa situação.**
É sempre uma config da impressora que desabilitou o Modbus (serial
ativa, protocolo trocado, etc).

### 20. Documentação do fabricante é incompleta e às vezes errada

Os três PDFs e a planilha Modbus do fabricante Codeline cobrem talvez
60% do que a impressora faz. Vários campos funcionam diferente do
descrito. Teste tudo em paralelo com a tela de comunicação da
impressora antes de confiar.
