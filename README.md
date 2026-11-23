# Comunicação entre dois PCs por RF433

O projeto usa dois ESP32. Cada placa tem um módulo TX e um RX RF433; o ESP32
receptor também tem o LCD Keypad Shield 16×2. Os arquivos `transmissor/transmissor.ino`
e `receptor/receptor.ino` são sketches completos para o Arduino IDE. Não é
necessária a biblioteca RCSwitch, RadioHead nem um arquivo `Rf433.h`.

## Ligações

Em **cada ESP32**: DATA do módulo TX → **D25**; DATA do módulo RX → resistor
**10 kΩ** → nó ligado ao **D26**; desse nó, resistor **20 kΩ** → **GND** do
mesmo ESP32. Alimente os módulos de acordo com o modelo usado na montagem e
una seus GND ao GND local. Não ligue a saída de 5 V do RX diretamente ao D26.

No ESP32 receptor, os pinos do shield LCD são: **D8 → D14** (RS), **D9 → D27**
(E), **D4 → D18**, **D5 → D19**, **D6 → D21**, **D7 → D22**. São os nomes
impressos nas placas. O LCD e os módulos RF precisam do GND comum com esse ESP32.

## Preparação

1. Grave `transmissor/transmissor.ino` no ESP32 transmissor e
   `receptor/receptor.ino` no ESP32 receptor, selecionando `ESP32 Dev Module`
   no Arduino IDE. Reinicie ambos para começar a sequência de quadros em zero.
2. Copie `pc_transfer.py` para **cada PC**. Cada cópia acessa somente o ESP32
   conectado por USB àquele computador. Os PCs não precisam compartilhar pasta,
   arquivo, rede ou número de porta COM. Instale Python e pyserial em ambos:
   `py -m pip install pyserial`.
3. Feche os Monitores Seriais do Arduino IDE. Só um programa pode abrir cada
   porta COM. Execute os comandos abaixo na pasta deste projeto.

No **PC receptor**, primeiro (o programa permanece aberto):

```powershell
py pc_transfer.py receive
```

No **outro PC**, ligado ao ESP32 transmissor, abra o menu interativo:

```powershell
py pc_transfer.py send
```

No menu, escolha **1** para digitar um texto, **2** para informar o caminho de
uma imagem (até 16 KiB), **3** para alternar entre **SW** (Stop-and-Wait) e
**SR** (Selective Repeat), **4** para reiniciar o ESP32 deste PC e **0** para
sair. O menu volta após cada envio; não é preciso executar o Python de novo.

Para um envio único sem o menu, também é possível usar:

```powershell
py pc_transfer.py send --text "OLA TESTE" --mode SW
py pc_transfer.py send --image .\figura.png --mode SR
```

No PC receptor, digite `/reset` e Enter para reiniciar o ESP32 conectado a
ele; `/sair` encerra. O ESP32 transmissor pode ser reiniciado pela opção 4 do
menu. Para reiniciar a placa local em uma execução separada, use
`py pc_transfer.py reset`. Cada Python controla sua própria placa USB; para
reiniciar **os dois** ESP32, use a opção de reset em **cada computador**.

Cada PC detecta automaticamente sua porta se tiver apenas um dispositivo USB
serial. Se houver mais de um, o programa lista as portas encontradas e você
escolhe a daquele ESP32 com `--port COMx` **no próprio PC**. O arquivo
`figura.png` precisa existir só no PC transmissor; o arquivo recebido é criado
só no PC receptor. Os dados entre placas passam pelos módulos RF433.

O PC receptor mostra o texto no terminal ou salva a imagem como
`imagem_recebida.png`/`.jpg`/`.gif`/`.bmp`, conforme a assinatura do arquivo.
Pode-se indicar `--output .\recebido.png`. O LCD exibe os primeiros 32 caracteres
imprimíveis de um texto e `Recebendo imagem` para uma imagem. A transferência
continua aguardando outras mensagens; `--once` encerra após uma. Para uma
ligação com menos ruído, `--chunk 4` reduz os bytes por quadro.

## Protocolo

Cada quadro RF é `FLAG | TYPE | SEQ | LEN | DATA | FCS`, com `FLAG=0x7E`,
`TYPE=1` DATA, `2` ACK e `3` END, e no máximo 16 bytes de DATA (21 bytes no
total). O FCS é **CRC-8**, polinômio `0x07`, inicial `0x00`, sem reflexão ou
XOR final, calculado sobre TYPE, SEQ, LEN e DATA. Cada byte segue MSB primeiro.
O sinal OOK usa 32 pulsos de preâmbulo, separador e largura do intervalo LOW
para codificar cada bit. Não usa biblioteca de protocolo RF.

O primeiro DATA de cada transferência traz `T` (texto) ou `I` (imagem) e o
tamanho total em uint32 little-endian. Os DATA seguintes trazem os bytes.
O END só é aceito após todos os bytes. No modo **SW**, a janela é de 1 quadro.
No modo **SR**, a janela é de 4 quadros: o transmissor acompanha o ACK de cada
SEQ e retransmite apenas os que faltarem; o receptor guarda quadros fora de
ordem e entrega os dados em ordem ao PC. Cada quadro admite até dez tentativas.
O receptor confirma quadros duplicados sem acrescentar seus bytes novamente. Um quadro
com CRC incorreto é descartado. O PC receptor reconstrói exatamente os bytes
recebidos; uma imagem não é convertida ou comprimida pelo programa.

**Falha definitiva (`@FAIL sem_ack`):** confira as duas direções RF e reinicie
ambos os ESP32 antes de repetir, para sincronizar a sequência. Os parâmetros
de pulso podem precisar de ajuste conforme os módulos e a interferência no local.

Para demonstrar a recuperação de SW ou SR, altere
`SIMULAR_PERDA_DE_ACK` para `true` no sketch receptor e grave-o novamente.
O primeiro ACK não será enviado: o transmissor registrará um timeout e
repetirá o quadro; o receptor reconhecerá o duplicado e enviará o ACK sem
duplicar os dados. Depois, volte o valor para `false` e grave outra vez.
