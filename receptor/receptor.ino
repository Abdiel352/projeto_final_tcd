#include <LiquidCrystal.h>

#include <Arduino.h>

// Quadro: FLAG | TYPE | SEQ | LEN | DATA[0..16] | CRC-8.
// Modulação OOK por largura do intervalo LOW, sem biblioteca de protocolo RF.
namespace Rf433 {
constexpr uint8_t TX_PIN = 25;
constexpr uint8_t RX_PIN = 26;
constexpr uint8_t FLAG = 0x7E;
constexpr uint8_t DATA = 1;
constexpr uint8_t ACK = 2;
constexpr uint8_t END = 3;
constexpr uint8_t MAX_DATA = 16;
constexpr uint8_t MAX_FRAME = 21;

struct Frame {
  uint8_t type;
  uint8_t seq;
  uint8_t len;
  uint8_t data[MAX_DATA];
};

static volatile uint8_t work[MAX_FRAME];
static volatile uint8_t ready[MAX_FRAME];
static volatile uint8_t workBits = 0;
static volatile uint8_t readyBytes = 0;
static volatile uint8_t expectedBytes = 0;
static volatile uint8_t rxState = 0;  // 0=sync HIGH, 1=sync LOW, 2=bits
static volatile bool highPulseOk = false;
static volatile bool transmitting = false;
static volatile uint32_t lastEdge = 0;
static volatile uint32_t pulsosRecebidos = 0;
static volatile uint32_t sincronismosRecebidos = 0;
static volatile uint32_t iniciosRecebidos = 0;
static volatile uint8_t maiorSequenciaDeBits = 0;
static volatile uint32_t cabecalhosRecebidos = 0;
static volatile uint32_t quadrosRecebidos = 0;

inline uint8_t crc8(const uint8_t *bytes, uint8_t count) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < count; ++i) {
    crc ^= bytes[i];
    for (uint8_t b = 0; b < 8; ++b) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07)
                         : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

void ARDUINO_ISR_ATTR onEdge() {
  const uint32_t now = micros();
  const uint32_t duration = now - lastEdge;
  lastEdge = now;
  if (transmitting) return;
  ++pulsosRecebidos;

  if (digitalRead(RX_PIN) == LOW) {  // Fim de um pulso HIGH
    if (rxState == 0 && duration >= 2500 && duration <= 7500) {
      ++sincronismosRecebidos;
      rxState = 1;
      workBits = 0;
      expectedBytes = 0;
      highPulseOk = false;
    } else if (rxState == 2) {
      highPulseOk = duration >= 180 && duration <= 1400;
      if (!highPulseOk) rxState = 0;
    }
    return;
  }

  // Fim de um intervalo LOW.
  if (rxState == 1) {
    if (duration >= 1000 && duration <= 4000) {
      ++iniciosRecebidos;
      rxState = 2;
    } else {
      rxState = 0;
    }
    return;
  }
  if (rxState != 2 || !highPulseOk) return;

  uint8_t bit;
  if (duration >= 180 && duration < 1100) bit = 0;
  else if (duration >= 1100 && duration <= 3000) bit = 1;
  else { rxState = 0; return; }

  const uint8_t byteIndex = workBits >> 3;
  if (byteIndex >= MAX_FRAME) { rxState = 0; return; }
  if ((workBits & 7) == 0) work[byteIndex] = 0;
  work[byteIndex] = static_cast<uint8_t>((work[byteIndex] << 1) | bit);
  ++workBits;
  if (workBits > maiorSequenciaDeBits) maiorSequenciaDeBits = workBits;
  highPulseOk = false;

  if (workBits == 32) {
    if (work[0] != FLAG || work[3] > MAX_DATA) {
      rxState = 0;
      return;
    }
    ++cabecalhosRecebidos;
    expectedBytes = 5 + work[3];
  }
  if (expectedBytes && workBits == expectedBytes * 8) {
    ++quadrosRecebidos;
    if (!readyBytes) {
      for (uint8_t i = 0; i < expectedBytes; ++i) ready[i] = work[i];
      readyBytes = expectedBytes;
    }
    rxState = 0;
  }
}

inline void begin() {
  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, LOW);
  pinMode(RX_PIN, INPUT);
  lastEdge = micros();
  attachInterrupt(digitalPinToInterrupt(RX_PIN), onEdge, CHANGE);
}

inline bool receive(Frame &frame, bool &badCrc) {
  uint8_t raw[MAX_FRAME];
  uint8_t count;
  noInterrupts();
  count = readyBytes;
  if (count) {
    for (uint8_t i = 0; i < count; ++i) raw[i] = ready[i];
    readyBytes = 0;
  }
  interrupts();
  if (!count) return false;
  badCrc = crc8(raw + 1, count - 2) != raw[count - 1];
  if (badCrc || raw[0] != FLAG || raw[3] > MAX_DATA || count != raw[3] + 5)
    return false;
  frame.type = raw[1];
  frame.seq = raw[2];
  frame.len = raw[3];
  for (uint8_t i = 0; i < frame.len; ++i) frame.data[i] = raw[4 + i];
  return true;
}

inline void pulse(uint16_t highUs, uint16_t lowUs) {
  digitalWrite(TX_PIN, HIGH);
  delayMicroseconds(highUs);
  digitalWrite(TX_PIN, LOW);
  delayMicroseconds(lowUs);
}

inline void send(uint8_t type, uint8_t seq, const uint8_t *data, uint8_t len) {
  if (len > MAX_DATA) return;
  uint8_t raw[MAX_FRAME] = {FLAG, type, seq, len};
  for (uint8_t i = 0; i < len; ++i) raw[4 + i] = data[i];
  raw[4 + len] = crc8(raw + 1, 3 + len);

  noInterrupts();
  transmitting = true;
  rxState = 0;
  interrupts();
  for (uint8_t i = 0; i < 32; ++i) pulse(500, 500);
  pulse(4000, 2000);  // Separador de início.
  for (uint8_t i = 0; i < len + 5; ++i) {
    for (int8_t b = 7; b >= 0; --b)
      pulse(500, (raw[i] & (1 << b)) ? 1500 : 500);
  }
  // A última transição LOW->HIGH permite ao receptor medir o último bit.
  digitalWrite(TX_PIN, HIGH);
  delayMicroseconds(500);
  digitalWrite(TX_PIN, LOW);
  delay(8);
  noInterrupts();
  lastEdge = micros();
  rxState = 0;
  transmitting = false;
  interrupts();
}
}  // namespace Rf433

// Pinagem do LCD que ja funcionou na montagem:
// Shield D8->ESP D14, D9->D27, D4->D18, D5->D19, D6->D21, D7->D22.
LiquidCrystal lcd(14, 27, 18, 19, 21, 22);
static uint8_t proximaSequencia = 0;
static uint8_t posicaoLcd = 0;
static bool transferenciaAtiva = false;
static char tipoTransferencia = 0;
static uint32_t restantes = 0;
static constexpr uint32_t MAX_BYTES = 16384;
// Mude para true para demonstrar uma retransmissao (o primeiro ACK e omitido).
static constexpr bool SIMULAR_PERDA_DE_ACK = false;
static bool perdaSimulada = false;

void mostrarNoLcd(const uint8_t *dados, uint8_t quantidade) {
  for (uint8_t i = 0; i < quantidade; ++i) {
    if (posicaoLcd >= 32) continue;
    lcd.setCursor(posicaoLcd % 16, posicaoLcd / 16);
    const uint8_t c = dados[i];
    lcd.write(c >= 32 && c <= 126 ? c : '?');
    ++posicaoLcd;
  }
}

void imprimirHex(const uint8_t *dados, uint8_t quantidade) {
  static const char digitos[] = "0123456789ABCDEF";
  Serial.print("@DATA ");
  for (uint8_t i = 0; i < quantidade; ++i) {
    Serial.write(digitos[dados[i] >> 4]);
    Serial.write(digitos[dados[i] & 15]);
  }
  Serial.println();
}

void confirmar(uint8_t seq) {
  if (SIMULAR_PERDA_DE_ACK && !perdaSimulada) {
    perdaSimulada = true;
    Serial.printf("Simulacao: ACK de SEQ=%u omitido uma vez\n", seq);
    return;
  }
  delay(40);
  Rf433::send(Rf433::ACK, seq, nullptr, 0);
  Serial.printf("ACK enviado, SEQ=%u\n", seq);
}

void setup() {
  Serial.begin(115200);
  lcd.begin(16, 2);
  lcd.print("Aguardando RF");
  Rf433::begin();
  Serial.println("@READY_RX");
}

void loop() {
  Rf433::Frame quadro;
  bool crcRuim = false;
  if (!Rf433::receive(quadro, crcRuim)) {
    if (crcRuim) Serial.println("CRC incorreto: quadro descartado");
    delay(1);
    return;
  }

  if (quadro.type != Rf433::DATA && quadro.type != Rf433::END) return;
  if (quadro.seq == proximaSequencia) {
    if (quadro.type == Rf433::DATA) {
      if (!transferenciaAtiva) {
        if (quadro.len != 5 || (quadro.data[0] != 'T' && quadro.data[0] != 'I')) return;
        const uint32_t total = static_cast<uint32_t>(quadro.data[1]) |
                               (static_cast<uint32_t>(quadro.data[2]) << 8) |
                               (static_cast<uint32_t>(quadro.data[3]) << 16) |
                               (static_cast<uint32_t>(quadro.data[4]) << 24);
        if (total > MAX_BYTES) return;
        tipoTransferencia = static_cast<char>(quadro.data[0]);
        restantes = total;
        transferenciaAtiva = true;
        lcd.clear();
        posicaoLcd = 0;
        if (tipoTransferencia == 'I') lcd.print("Recebendo imagem");
        Serial.printf("@START %c %lu\n", tipoTransferencia,
                      static_cast<unsigned long>(total));
      } else {
        if (quadro.len == 0 || quadro.len > restantes) return;
        imprimirHex(quadro.data, quadro.len);
        if (tipoTransferencia == 'T') mostrarNoLcd(quadro.data, quadro.len);
        restantes -= quadro.len;
      }
    } else {
      if (!transferenciaAtiva || quadro.len != 0 || restantes != 0) return;
      Serial.println("@END");
      transferenciaAtiva = false;
    }
    ++proximaSequencia;
    confirmar(quadro.seq);
  } else if (quadro.seq == static_cast<uint8_t>(proximaSequencia - 1)) {
    Serial.printf("Quadro duplicado SEQ=%u\n", quadro.seq);
    confirmar(quadro.seq);
  } else {
    Serial.printf("SEQ fora de ordem: %u (esperado %u)\n", quadro.seq, proximaSequencia);
  }
}
