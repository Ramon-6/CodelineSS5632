# Ligação física do painel

Como ligar o StamPLC na impressora, na esteira, no botão de modo manual e
na sirene de limpeza. Esta é a versão final depois das últimas revisões.

## Lista de materiais

| Item | Quantidade | Obs |
|---|---|---|
| M5Stack StamPLC | 1 | módulo principal |
| M5Stack módulo PoE W5500 | 1 | encaixa em cima do StamPLC — dá RJ45 |
| Fonte 24 V DC industrial | 1 | mínimo 2 A, com borne DIN |
| Switch ethernet 10/100 | 1 | 4 portas é suficiente |
| Cabo RJ45 CAT5e/6 | 3 | PLC, impressora, notebook de serviço |
| Sirene 24 V DC | 1 | piezo ou mecânica, qualquer uma |
| Botão NA 24 V (modo manual) | 1 | com retorno por mola, pode ser pulsante |
| Caixa de passagem IP54 | 1 | para proteger o painel |

A impressora Codeline SS5632 e a esteira já existem — nenhum fio delas
muda. Só adicionamos derivações.

## Alimentação

```
                    ┌──── V+ do StamPLC (borne vermelho)
Fonte 24 V DC ─ +24V┤
                    └──── +24V do barramento (referência de IN1, IN2 e sirene)

                    ┌──── V- do StamPLC (borne preto)
                ─ 0V┤
                    └──── 0V do barramento = COMUM DAS ENTRADAS (COM-)
```

O StamPLC aceita 6 a 36 V. 24 V é a opção padrão industrial — a mesma
tensão do LED do botão da esteira, dos sensores e da sirene.

## Rede ethernet

```
┌──────────┐      ┌────────┐      ┌─────────────┐
│  StamPLC │──RJ45│ Switch │RJ45──│ Impressora  │
│   (PoE)  │      │  5p    │      │   SS5632    │
└──────────┘      └────────┘      └─────────────┘
                      │
                     RJ45
                      │
                 Notebook de
                 configuração
                 (opcional)
```

**IPs fixos** (conferir no `/config` do PLC):

| Equipamento | IP |
|---|---|
| StamPLC | 192.168.1.50 |
| Impressora | 192.168.1.100 |
| Gateway | 192.168.1.1 |
| Máscara | 255.255.255.0 |

Notebook de configuração entra na mesma faixa.

## Esteira: botão pulsante + LED

A esteira tem um botão pulsante próprio no painel do operador. Esse
botão continua funcionando normalmente — a gente só liga o relé R1 do
PLC **em paralelo** com os terminais desse botão, para que o PLC possa
pulsar sem o operador apertar.

```
Botão pulsante original da esteira (NA):
                      ┌──────────────────── para o CLP da esteira
    [Terminal A] ─────┤
                      └──────── R1 NA do StamPLC
                      
                      ┌──────────────────── 0V ou retorno
    [Terminal B] ─────┤
                      └──────── R1 COM do StamPLC
```

O relé do PLC fecha por 1 segundo (PULSO_MS no config.h) e abre.
Resultado: a esteira recebe o mesmo "clique" que o operador daria
no botão.

**LED do botão (24 V, aceso quando esteira ligada):**

```
[+ do LED]  ─── IN1 do StamPLC
[- do LED]  ─── 0V (barramento comum)
```

**ATENÇÃO**: tem que puxar o fio do LED **após o CLP da esteira**, não
antes. Senão o IN1 vai ler "ligada" só porque o painel está energizado,
não porque a esteira está realmente rodando.

## Botão de modo manual (IN2)

Botão NA externo que o operador aperta pra pulsar o relé no modo
MANUAL. Vai em qualquer lugar visível no painel.

```
    ┌─ Terminal 1 ───── +24V (barramento)
    │
Botão NA
    │
    └─ Terminal 2 ───── IN2 do StamPLC
```

- Modo AUTO (default): PLC ignora esse botão. Pode apertar à vontade,
  nada acontece.
- Modo MANUAL (trocado na tela do PLC em `/config`): cada aperto
  do botão pulsa R1 por 1s. É como se o operador apertasse direto o
  botão da esteira.

## Sirene de limpeza (R2)

Sirene 24 V DC qualquer. Dispara nos horários 07:00, 16:00 e 00:00
quando o PLC detecta que precisa limpar o cabeçote. Só desliga quando
o operador abre a tampa do cabeçote por 5s+ e fecha de novo (o
PLC lê o estado da tampa via Modbus — sem fio físico pra ela).

```
[+24V]  ──────── R2 COM do StamPLC
                     │
                 R2 NA
                     │
                     ├────── [+ da sirene]
                     
                 [- da sirene] ──── 0V (barramento)
```

Se a sirene for 110/220V AC, o relé do PLC aguenta (5A). Mas aí precisa
ligar na rede, não no barramento 24V.

## Impressora Codeline SS5632

**Nenhuma fiação muda**. Mantém:
- Encoder (conector de 7 pinos atrás da impressora) ligado na esteira
- Fotocélula (sensor de 5 pinos) na frente da esteira
- Leitor de código USB plugado na impressora
- Alimentação própria 110/220V da impressora
- RJ45 → switch de rede

**Configuração que não pode mexer** (ver `docs/02-portas-rede.md`):
- Tela "Serial Port Setting" → campo **Protocolo = None**
  (qualquer outra opção desabilita o Modbus TCP e o PLC para de
   conectar — já aconteceu uma vez no projeto)

## Fins de curso que NÃO precisam de fio

A impressora já tem internamente:
- Fim de curso da tampa do cabeçote (bit Modbus `1x0018`)
- Fotocélula de produto na esteira
- Encoder de posição

Tudo isso o PLC lê **pela rede Modbus**. Não precisa puxar fio físico
dos sensores pro PLC.

## Resumo das conexões no StamPLC

| Terminal | Para onde | Função |
|---|---|---|
| V+ | +24V da fonte | Alimentação PLC |
| V- | 0V da fonte | GND |
| R1 COM / R1 NA | Botão pulsante da esteira (paralelo) | Pulsa esteira |
| R2 COM / R2 NA | Sirene | Alerta limpeza |
| IN1 | LED 24V do botão da esteira | Confirma esteira ligada |
| IN2 | Botão NA pulsante externo | Modo manual |
| COM- | 0V da fonte | Comum das entradas |
| RJ45 (PoE) | Switch | Rede |

**R3 e R4 ficam livres** para expansão futura (ex: ligar outra
sinaleira pros estados, ou isolar a sirene em outra tensão).

## Teste rápido depois de ligar tudo

1. **Alimenta a fonte**. StamPLC acende, mostra "Iniciando...".
2. **Confirma rede**: na tela ESTADOS do PLC, "Impressora" deve virar
   verde em alguns segundos.
3. **Testa R1**: aperta o botão B do próprio StamPLC (em frente à
   tela do painel). Esteira deve ligar por 1 segundo (pulso), aí o PLC
   espera o LED do botão subir — se não subir em 5s, dá ERRO (que
   auto-recupera em 5s).
4. **Testa IN1**: liga a esteira pelo botão físico. Na tela ESTADOS do
   PLC, "Esteira" deve ficar verde.
5. **Testa IN2** (modo manual): troca pra MANUAL em `/config`, aperta o
   botão externo. Esteira deve pulsar.
6. **Testa R2 (sirene)**: via `/config` ativa sirene e troca o horário
   para 1 min no futuro. Espera chegar. Sirene liga.
   - Abre tampa da impressora por 5+ segundos, fecha. Sirene desliga.
   - Repete o teste encostando rápido (< 5s). Sirene continua.
7. **Testa impressora**: bipa um código de barras no leitor da
   impressora. PLC detecta, esteira liga, tempo corre, para no fim e
   apaga o código.
