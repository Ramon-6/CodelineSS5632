# Integração com câmera IA — futura, em andamento

**Status**: planejamento. Nada implementado ainda. Esta pasta é a trilha
para substituir o modo "roda por X minutos" por **"roda enquanto a câmera
estiver vendo peça"**.

## O que vai mudar

### Hoje (modo tempo)
1. Operador lê código no scanner da impressora.
2. StamPLC detecta leitura, liga esteira.
3. Esteira roda pelo tempo configurado (ex: 10 min).
4. StamPLC desliga esteira e apaga o código.

### Depois (modo visão)
1. Operador lê código no scanner da impressora.
2. StamPLC detecta leitura, liga esteira.
3. **Câmera com IA vigia a entrada da esteira** e informa ao PLC
   continuamente se tem ou não peça passando.
4. Esteira continua rodando enquanto a câmera vê peça.
5. **Sem peça por N segundos → PLC desliga a esteira** e apaga o código.
6. Nova peça aparece sem código novo → PLC alerta (buzzer) ou mantém
   parado, dependendo da política.

Vantagens:
- Não desperdiça tempo esperando a esteira acabar quando o lote já saiu.
- Não trava com peça atrasada (continua enquanto chegar peça).
- Dá dados de contagem real (peças/hora, não tempo decorrido).

## Por que não é I2C

**Câmera via I2C puro não existe na prática.** I2C é um barramento serial
lento (100 kbit/s a 1 Mbit/s) usado para sensores de temperatura, acelerômetros,
expansores de I/O. Não transporta vídeo.

Os sensores de imagem (CMOS) usam **DVP, MIPI-CSI, SPI ou USB**. O que o
mercado vende como "câmera com IA" é um **módulo completo** com câmera + CPU +
rede neural rodando dentro — ele faz a inferência e manda **só o resultado**
("tem peça = sim/não", ou coordenadas da peça).

Esse resultado é um dado pequeno, e **aí sim** cabe num protocolo serial.
Como o StamPLC tem porta Grove, usamos **UART** (mesmo conector físico do
I2C, pinos diferentes).

## Opções de hardware

Comparação detalhada em [hardware.md](hardware.md). Resumo:

| Hardware | Preço | IA onde | Como fala com PLC | Dificuldade |
|---|---|---|---|---|
| **M5Stack Module LLM** (Ax630C) | ~R$ 900 | no próprio módulo | UART Grove | média |
| **M5Stack UnitV2** | ~R$ 650 | no próprio módulo | UART Grove ou USB | média |
| **ESP32-CAM + servidor local** | ~R$ 60 + PC | no PC/servidor | WiFi/HTTP | alta |
| **Câmera IP + VLM na nuvem** | varia | nuvem | WiFi/HTTP | alta |
| **Sensor fotoelétrico simples** | ~R$ 50 | nenhum | 24 V na IN2 | baixa |

**Caminho recomendado**: começar pelo sensor fotoelétrico (opção mais barata
e simples, resolve 90% do problema), e só evoluir para visão com IA se
precisar discriminar peças (ex: ignorar lixo passando, contar peças por
tipo).

## Documentos nesta pasta

- [hardware.md](hardware.md) — comparação detalhada das opções
- [arquitetura.md](arquitetura.md) — como a câmera e o PLC se conversam
- [fase-1-sensor.md](fase-1-sensor.md) — primeira fase: sensor fotoelétrico
- [fase-2-camera.md](fase-2-camera.md) — segunda fase: câmera com IA
- [codigo-exemplo.md](codigo-exemplo.md) — esqueleto de firmware

## Autor

Ramon · [github.com/Ramon-6/CodelineSS5632](https://github.com/Ramon-6/CodelineSS5632)
