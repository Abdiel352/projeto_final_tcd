
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

  if (digitalRead(RX_PIN) == LOW) {  // Fim de um pulso HIGH
    if (rxState == 0 && duration >= 2500 && duration <= 7500) {
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
    rxState = (duration >= 1000 && duration <= 4000) ? 2 : 0;
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
  highPulseOk = false;

  if (workBits == 32) {
    if (work[0] != FLAG || work[3] > MAX_DATA) {
      rxState = 0;
      return;
    }
    expectedBytes = 5 + work[3];
  }
  if (expectedBytes && workBits == expectedBytes * 8) {
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


// USB/Serial: HELLO, START T|I <bytes>, CHUNK <hex>.
// O primeiro DATA RF traz metadados: tipo (T ou I) e tamanho uint32 LE.
// Os DATA seguintes trazem o arquivo em blocos de ate 16 bytes.
static constexpr uint32_t MAX_BYTES = 16384;
static char linha[80];
static uint8_t usados = 0;
static uint32_t restantes = 0;
static uint8_t sequencia = 0;
static bool transferenciaAtiva = false;

bool enviarComAck(uint8_t tipo, const uint8_t *dados, uint8_t quantidade) {
  for (uint8_t tentativa = 1; tentativa <= 10; ++tentativa) {
    Serial.printf("TX tipo=%u seq=%u tentativa=%u\n", tipo, sequencia, tentativa);
    Rf433::send(tipo, sequencia, dados, quantidade);
    const uint32_t inicio = millis();
    while (millis() - inicio < 2000) {
      Rf433::Frame resposta;
      bool crcRuim = false;
      if (Rf433::receive(resposta, crcRuim) &&
          resposta.type == Rf433::ACK && resposta.seq == sequencia &&
          resposta.len == 0) {
        Serial.printf("ACK recebido seq=%u\n", sequencia);
        ++sequencia;
        return true;
      }
      if (crcRuim) Serial.println("ACK com CRC incorreto");
      delay(1);
    }
    Serial.println("Timeout: retransmitindo");
  }
  Serial.printf("@FAIL sem_ack seq=%u\n", sequencia);
  transferenciaAtiva = false;
  return false;
}

int8_t hexValor(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void terminar() {
  if (enviarComAck(Rf433::END, nullptr, 0)) Serial.println("@DONE");
  transferenciaAtiva = false;
}

void processarLinha() {
  if (strcmp(linha, "HELLO") == 0) {
    Serial.println("@READY");
    return;
  }

  if (strncmp(linha, "START ", 6) == 0) {
    if (transferenciaAtiva) { Serial.println("@FAIL ocupado"); return; }
    const char tipo = linha[6];
    if ((tipo != 'T' && tipo != 'I') || linha[7] != ' ') {
      Serial.println("@FAIL start_invalido");
      return;
    }
    char *fim = nullptr;
    const unsigned long total = strtoul(linha + 8, &fim, 10);
    if (fim == linha + 8 || *fim != '\0' || total > MAX_BYTES) {
      Serial.println("@FAIL tamanho_invalido");
      return;
    }
    restantes = static_cast<uint32_t>(total);
    uint8_t meta[5] = {static_cast<uint8_t>(tipo),
                       static_cast<uint8_t>(total),
                       static_cast<uint8_t>(total >> 8),
                       static_cast<uint8_t>(total >> 16),
                       static_cast<uint8_t>(total >> 24)};
    transferenciaAtiva = true;
    if (!enviarComAck(Rf433::DATA, meta, 5)) return;
    if (restantes == 0) terminar();
    else Serial.printf("@NEXT %lu\n", static_cast<unsigned long>(restantes));
    return;
  }

  if (strncmp(linha, "CHUNK ", 6) == 0) {
    if (!transferenciaAtiva) { Serial.println("@FAIL sem_start"); return; }
    const size_t nHex = strlen(linha + 6);
    const size_t n = nHex / 2;
    if (nHex == 0 || (nHex & 1) || n > Rf433::MAX_DATA || n > restantes) {
      Serial.println("@FAIL chunk_invalido");
      transferenciaAtiva = false;
      return;
    }
    uint8_t dados[Rf433::MAX_DATA];
    for (size_t i = 0; i < n; ++i) {
      const int8_t a = hexValor(linha[6 + 2 * i]);
      const int8_t b = hexValor(linha[7 + 2 * i]);
      if (a < 0 || b < 0) {
        Serial.println("@FAIL hex_invalido");
        transferenciaAtiva = false;
        return;
      }
      dados[i] = static_cast<uint8_t>((a << 4) | b);
    }
    if (!enviarComAck(Rf433::DATA, dados, static_cast<uint8_t>(n))) return;
    restantes -= n;
    if (restantes == 0) terminar();
    else Serial.printf("@NEXT %lu\n", static_cast<unsigned long>(restantes));
    return;
  }
  Serial.println("@FAIL comando_invalido");
}

void setup() {
  Serial.begin(115200);
  Rf433::begin();
  Serial.println("@READY");
}

void loop() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      linha[usados] = '\0';
      if (usados) processarLinha();
      usados = 0;
    } else if (usados < sizeof(linha) - 1) {
      linha[usados++] = c;
    } else {
      usados = 0;
      Serial.println("@FAIL linha_longa");
    }
  }
}
