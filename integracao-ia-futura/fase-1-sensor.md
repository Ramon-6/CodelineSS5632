# Fase 1 — Sensor fotoelétrico (sem IA)

Antes de IA, vale medir se um sensor simples resolve. Em 90% dos casos
industriais, resolve.

## Shopping list

- 1x sensor fotoelétrico retrorreflexivo 24 V DC, saída PNP NA
  (procurar "sensor fotoelétrico M18 PNP NA")
- 1x refletor (geralmente acompanha o sensor)
- 2 metros de cabo 2x0,5 mm²
- 1x suporte de fixação (pode ser imã se a esteira for metálica)

Fornecedores: Autonics, Omron, Sick, Pepperl+Fuchs. Modelos genéricos
chineses funcionam para esse uso.

Orçamento: R$ 50-300 dependendo da marca.

## Ligação

```
Sensor fotoelétrico:
  BR  (marrom) ──── +24 V da fonte do PLC
  AZ  (azul)   ──── 0 V da fonte
  PR  (preto)  ──── IN2 do StamPLC
                    (COM das entradas no 0 V da mesma fonte)
```

Posicionar sensor + refletor nas laterais da entrada da esteira,
alinhados pela altura da peça, de modo que a passagem da peça interrompa
o feixe.

## Mudança no firmware

Pouca coisa. No `config.h`:

```cpp
#define CAMERA_HABILITADA 1
#define CAMERA_PINO       1    // IN2 (0=IN1, 1=IN2, ...)
#define CAMERA_SEM_PECA_S 15   // sem peça por 15 s = desliga esteira
```

Na máquina de estados, substituir a saída temporizada do estado
`RODANDO` por:

```cpp
case RODANDO: {
    bool temPeca = PLC.readInput(CAMERA_PINO);
    if (temPeca) {
        ultimaPecaVista = millis();
    }
    if (millis() - ultimaPecaVista > CAMERA_SEM_PECA_S * 1000UL) {
        irPara(PULSA_PARA);
    }
    // mantém o fallback de tempo máximo
    if (millis() - inicioRodando > TEMPO_MAX_ABSOLUTO_MS) {
        irPara(PULSA_PARA);
    }
    break;
}
```

`TEMPO_MAX_ABSOLUTO_MS` evita que a esteira rode para sempre se o
sensor travar vendo "sempre tem peça" (feixe obstruído por mosca,
poeira, etc). Setar em 2x o tempo máximo esperado para um lote (ex: 30 min).

## Teste de campo

1. Montar sensor e refletor na bancada antes de ir para a esteira.
2. Passar objeto no feixe, confirmar LED do sensor acendendo e IN2
   do PLC alternando.
3. Levar para a esteira, calibrar altura.
4. Simular um lote completo:
   - Ler código no scanner
   - Observar esteira ligar
   - Passar peças pelo feixe → esteira continua
   - Parar de passar peças → esteira desliga em 15 s
   - Confirmar que `0x33` apagou o código na impressora

## Fallback

Se o sensor estiver em curto ou desconectado (IN2 constante em 0), o
PLC entra em `TEMPO_MAX_ABSOLUTO_MS` e para. Se estiver sempre em 1
(feixe sempre interrompido), o PLC fica em `RODANDO` indefinidamente —
por isso o TEMPO_MAX é obrigatório.

## Reversão

Para desligar o sensor e voltar ao modo tempo puro:

```cpp
#define CAMERA_HABILITADA 0
```

Nada mais muda. Firmware mantém compatibilidade.
