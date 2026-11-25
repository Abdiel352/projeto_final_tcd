# Comunicação confiável entre dois PCs por RF 433 MHz

Projeto da disciplina **Transmissão e Comunicação de Dados**. Dois computadores
trocam textos e pequenas imagens por meio de dois ESP32 e módulos RF 433 MHz.
Cada computador acessa seu ESP32 por USB; a comunicação entre as placas usa
rádio. O receptor mostra o texto em um LCD 16×2 e reconstrói a imagem no PC.

## Estrutura do repositório

```text
firmware/
  transmissor/transmissor.ino  Sketch do ESP32 transmissor
  receptor/receptor.ino        Sketch do ESP32 receptor e LCD
pc/pc_transfer.py             Programa de envio, recepção e reset
docs/
  montagem.md                 Componentes e ligações
  uso.md                      Instalação, comandos e solução de problemas
  protocolo.md                Quadro RF, CRC-8 e recuperação de erros
  evidencias/README.md        Orientação para guardar registros de testes
requirements.txt              Dependência Python
```

Os sketches são completos e devem ser abertos em suas respectivas pastas no
Arduino IDE. O mesmo programa Python é usado nos dois computadores.

## Início rápido

1. Faça as ligações descritas em [montagem](docs/montagem.md).
2. Grave os sketches nas respectivas placas, selecionando `ESP32 Dev Module`
   no Arduino IDE.
3. Em **cada PC**, instale Python e execute `py -m pip install -r requirements.txt`
   na raiz do projeto. Se copiar apenas `pc/pc_transfer.py`, instale `pyserial`
   em ambos com `py -m pip install pyserial`.
4. Feche os Monitores Seriais do Arduino IDE e reinicie as placas.
5. No PC receptor, execute `py pc/pc_transfer.py receive`.
6. No PC transmissor, execute `py pc/pc_transfer.py send`.

Os demais comandos estão em [uso](docs/uso.md). A estrutura do quadro e o
cálculo do FCS estão em [protocolo](docs/protocolo.md).

## Relação com a proposta

O código-fonte dos dois ESP32 está em `firmware/` e o programa dos computadores
em `pc/`. As instruções de montagem, uso e protocolo estão em `docs/`.
Registros reais de transmissão, LCD, reconstrução da imagem e testes de alcance
ou ruído podem ser guardados em `docs/evidencias/` quando forem produzidos. O
repositório não afirma que essas demonstrações já foram realizadas.
