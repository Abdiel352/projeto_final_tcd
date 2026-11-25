# Protocolo RF

## Estrutura do quadro

```text
FLAG | TYPE | SEQ | LEN | DATA (0 a 16 bytes) | FCS
```

Todos os campos fora de `DATA` ocupam um byte; um quadro tem no máximo 21
bytes. `FLAG` vale `0x7E`. `TYPE` vale `1` para DATA, `2` para ACK e `3` para
END. `SEQ` identifica o quadro e `LEN` indica quantos bytes existem em `DATA`.

O primeiro quadro DATA de uma transferência contém 5 bytes de metadados: `T`
para texto ou `I` para imagem, seguidos do tamanho total em um inteiro de 32
bits little-endian. Os quadros DATA seguintes carregam o conteúdo. END sinaliza
o fim e só é aceito pelo receptor após a entrega de todos os bytes.

## Cálculo e verificação do FCS

O FCS implementado é **CRC-8**: polinômio `0x07`, valor inicial `0x00`, sem
reflexão e sem XOR final. O cálculo percorre, nessa ordem, `TYPE`, `SEQ`,
`LEN` e os `LEN` bytes de `DATA`; `FLAG` e o próprio FCS ficam fora do cálculo.

Para cada byte, o algoritmo faz XOR com o CRC atual. Em seguida, repete oito
vezes: desloca o CRC um bit à esquerda e, se o bit mais significativo antes do
deslocamento era 1, aplica XOR com `0x07`. Somente os oito bits inferiores são
mantidos. O transmissor coloca o resultado no último byte do quadro. O receptor
repete o cálculo e descarta o quadro quando o resultado difere do FCS recebido.

## Transmissão e recuperação

O sinal OOK usa 32 pulsos de preâmbulo, um separador e intervalos LOW de
duração diferente para representar cada bit, enviado do mais significativo
para o menos significativo. Os sketches implementam essa transmissão sem
biblioteca de protocolo RF.

- **SW (Stop-and-Wait):** janela de um quadro. O transmissor espera o ACK antes
  de avançar.
- **SR (Selective Repeat):** janela de quatro quadros. O transmissor acompanha
  os ACKs individualmente e retransmite os quadros ainda sem confirmação. O
  receptor guarda quadros fora de ordem e entrega os dados ao PC em ordem.

Quando o ACK não chega até o tempo configurado, o quadro é reenviado. Cada
quadro admite até dez tentativas. O receptor confirma quadros duplicados sem
entregar seus bytes novamente. O END também é confirmado por ACK.

Para demonstrar uma perda de ACK, a constante `SIMULAR_PERDA_DE_ACK` no sketch
do receptor pode ser alterada temporariamente para `true` e o sketch regravado.
O primeiro ACK é omitido, permitindo observar o timeout, a retransmissão e o
tratamento de duplicata nos logs. O valor normal do código é `false`.
