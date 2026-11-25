# Montagem do hardware

## Componentes

- 2 computadores e 2 placas ESP32;
- 2 módulos transmissores e 2 módulos receptores RF 433 MHz;
- 1 LCD Keypad Shield 16×2, no ESP32 receptor;
- Jumpers, protoboard e resistores de 10 kΩ e 20 kΩ para cada entrada RX.

## Ligações RF em cada ESP32

| Ponto | Ligação |
| --- | --- |
| DATA do transmissor RF | GPIO 25 (`D25`) |
| DATA do receptor RF | Resistor de 10 kΩ até o nó do GPIO 26 (`D26`) |
| Nó do GPIO 26 | Resistor de 20 kΩ até GND |
| GND dos módulos | GND da placa ESP32 correspondente |

O par de resistores forma um divisor de tensão para a saída do receptor RF.
Não ligue uma saída de 5 V diretamente ao GPIO 26. Alimente os módulos conforme
o modelo usado e mantenha o GND comum entre os módulos e o ESP32 de cada lado.

## LCD no ESP32 receptor

Os nomes `D` à esquerda são os pinos impressos no shield LCD.

| Pino do shield | GPIO do ESP32 | Função |
| --- | --- | --- |
| D8 | D14 | RS |
| D9 | D27 | E |
| D4 | D18 | D4 |
| D5 | D19 | D5 |
| D6 | D21 | D6 |
| D7 | D22 | D7 |

O LCD deve compartilhar o GND com o ESP32 receptor. Confira a identificação
dos pinos se outro modelo de shield for usado.
