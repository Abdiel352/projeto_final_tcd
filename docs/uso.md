# Uso do sistema

## Preparação

1. Abra `firmware/transmissor/transmissor.ino` e
   `firmware/receptor/receptor.ino` no Arduino IDE, cada um em sua própria pasta.
   Selecione `ESP32 Dev Module` e grave nas placas correspondentes.
2. Em ambos os PCs, instale Python e a dependência:

   ```powershell
   py -m pip install -r requirements.txt
   ```

   Se copiar só `pc/pc_transfer.py`, instale com `py -m pip install pyserial`.
3. Conecte cada ESP32 ao computador local por USB, feche o Monitor Serial do
   Arduino IDE e reinicie as placas para iniciar a sequência de quadros em zero.

## Receber e enviar

No PC receptor, inicie primeiro:

```powershell
py pc/pc_transfer.py receive
```

No PC transmissor, abra o menu:

```powershell
py pc/pc_transfer.py send
```

O menu permite enviar texto (1), selecionar imagem (2), alternar entre
Stop-and-Wait e Selective Repeat (3), reiniciar a placa local (4), alterar a
espera pelo ACK (5) e sair (0). A espera padrão é de 3000 ms, ajustável entre
500 e 10000 ms.

Envios únicos também funcionam sem o menu:

```powershell
py pc/pc_transfer.py send --text "OLA TESTE" --mode SW
py pc/pc_transfer.py send --image .\figura.png --mode SR
py pc/pc_transfer.py send --text "OLA" --ack-timeout 5000
```

O limite atual por texto ou imagem é **16 KiB**. O tamanho padrão dos dados
por quadro é 8 bytes; `--chunk 4` reduz esse tamanho e `--chunk` aceita de 1 a
16 bytes. O arquivo de origem precisa existir apenas no PC transmissor.

No PC receptor, o texto aparece no terminal e os primeiros 32 caracteres
imprimíveis aparecem no LCD. Para imagem, o LCD mostra `Recebendo imagem` e o
programa salva `imagem_recebida` com extensão `.png`, `.jpg`, `.gif` ou `.bmp`
conforme a assinatura do arquivo; outros formatos recebem `.bin`. Use
`--output .\recebido.png` para escolher a saída ou `--once` para encerrar após
uma transferência. O programa preserva os bytes da imagem.

## Porta serial, reset e falhas

O programa detecta a porta se houver apenas um dispositivo serial USB. Com
várias portas, informe `--port COMx` **no computador correspondente**. Cada
computador controla somente sua placa USB; os dados entre placas passam pelo RF.

No receptor, `/reset` reinicia a placa local e `/sair` encerra o programa. No
transmissor, use a opção 4 do menu. Também existe `py pc/pc_transfer.py reset`
para reiniciar a placa conectada ao PC onde o comando é executado. Para
reiniciar as duas placas, execute um reset em cada computador.

Se ocorrer `@FAIL sem_ack`, confira as duas direções RF, as ligações e o GND.
Reinicie ambos os ESP32 antes de tentar novamente. Os parâmetros de pulso
podem precisar de ajuste conforme os módulos e a interferência no local.
