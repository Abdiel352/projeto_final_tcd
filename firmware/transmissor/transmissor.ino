
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


// O PC carrega ate 16 KiB pela USB; depois os quadros seguem por RF.
// SW usa janela 1; SR usa janela 4 e retransmite so os quadros sem ACK.
static constexpr uint32_t MAX_BYTES = 16384;
static constexpr uint8_t SR_WINDOW = 4;
static constexpr uint32_t RF_GAP_MS = 180;
static constexpr uint8_t MAX_TRIES = 10;
static uint32_t ackTimeoutMs = 3000;
static uint8_t arquivo[MAX_BYTES];
static char linha[80];
static uint8_t usados = 0;
static uint32_t tamanho = 0;
static uint32_t carregados = 0;
static uint8_t sequencia = 0;
static uint8_t tamanhoQuadro = 8;
static char tipoTransferencia = 'T';
static bool transferenciaAtiva = false;
static bool seletivo = false;

struct SlotTx {
  uint16_t indice;
  uint32_t enviadoEm;
  uint8_t tentativas;
  bool ativo;
  bool confirmado;
};

void montarQuadro(uint16_t indice, uint8_t &tipo, const uint8_t *&dados,
                  uint8_t &quantidade, uint8_t meta[5]) {
  if (indice == 0) {
    tipo = Rf433::DATA;
    meta[0] = static_cast<uint8_t>(tipoTransferencia);
    meta[1] = static_cast<uint8_t>(tamanho);
    meta[2] = static_cast<uint8_t>(tamanho >> 8);
    meta[3] = static_cast<uint8_t>(tamanho >> 16);
    meta[4] = static_cast<uint8_t>(tamanho >> 24);
    dados = meta;
    quantidade = 5;
  } else if (indice == 1 + (tamanho + tamanhoQuadro - 1) / tamanhoQuadro) {
    tipo = Rf433::END;
    dados = nullptr;
    quantidade = 0;
  } else {
    tipo = Rf433::DATA;
    const uint32_t inicio = static_cast<uint32_t>(indice - 1) * tamanhoQuadro;
    dados = arquivo + inicio;
    const uint32_t faltam = tamanho - inicio;
    quantidade = faltam < tamanhoQuadro ? faltam : tamanhoQuadro;
  }
}

int8_t hexValor(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void enviarSlot(SlotTx &slot) {
  uint8_t meta[5];
  uint8_t tipo, quantidade;
  const uint8_t *dados;
  montarQuadro(slot.indice, tipo, dados, quantidade, meta);
  ++slot.tentativas;
  Serial.printf("TX %s indice=%u SEQ=%u tentativa=%u\n", seletivo ? "SR" : "SW",
                slot.indice, static_cast<uint8_t>(sequencia + slot.indice),
                slot.tentativas);
  Rf433::send(tipo, static_cast<uint8_t>(sequencia + slot.indice), dados, quantidade);
  slot.enviadoEm = millis();
}

bool transmitirArquivo() {
  const uint16_t total = 2 + (tamanho + tamanhoQuadro - 1) / tamanhoQuadro;
  const uint8_t janela = seletivo ? SR_WINDOW : 1;
  SlotTx slots[SR_WINDOW] = {};
  uint16_t base = 0, proximo = 0;
  uint32_t ultimaTransmissao = millis() - RF_GAP_MS;
  while (base < total) {
    Rf433::Frame resposta;
    bool crcRuim = false;
    if (Rf433::receive(resposta, crcRuim) &&
        resposta.type == Rf433::ACK && resposta.len == 0) {
      for (uint16_t i = base; i < proximo; ++i) {
        SlotTx &slot = slots[i % SR_WINDOW];
        if (slot.ativo && slot.indice == i &&
            resposta.seq == static_cast<uint8_t>(sequencia + i)) {
          slot.confirmado = true;
          Serial.printf("ACK SEQ=%u\n", resposta.seq);
          break;
        }
      }
      while (base < proximo && slots[base % SR_WINDOW].confirmado) {
        slots[base % SR_WINDOW].ativo = false;
        ++base;
        Serial.printf("@PROGRESS %u/%u\n", base, total);
      }
    } else if (crcRuim) {
      Serial.println("ACK com CRC incorreto");
    }
    if (base == total) break;

    const uint32_t agora = millis();
    if (agora - ultimaTransmissao >= RF_GAP_MS) {
      SlotTx *reenviar = nullptr;
      for (uint16_t i = base; i < proximo; ++i) {
        SlotTx &slot = slots[i % SR_WINDOW];
        if (slot.ativo && !slot.confirmado && agora - slot.enviadoEm >= ackTimeoutMs) {
          reenviar = &slot;
          break;
        }
      }
      if (reenviar) {
        if (reenviar->tentativas >= MAX_TRIES) {
          Serial.printf("@FAIL sem_ack seq=%u\n",
                        static_cast<uint8_t>(sequencia + reenviar->indice));
          return false;
        }
        Serial.println("Timeout: retransmitindo quadro sem ACK");
        enviarSlot(*reenviar);
        ultimaTransmissao = millis();
      } else if (proximo < total && proximo - base < janela) {
        SlotTx &slot = slots[proximo % SR_WINDOW];
        slot = {proximo, 0, 0, true, false};
        enviarSlot(slot);
        ++proximo;
        ultimaTransmissao = millis();
      }
    }
    delay(1);
  }
  sequencia = static_cast<uint8_t>(sequencia + total);
  Serial.println("@DONE");
  return true;
}

void processarLinha() {
  if (strcmp(linha, "HELLO") == 0) {
    Serial.println("@READY");
    return;
  }
  if (strcmp(linha, "RESET") == 0) {
    Serial.println("@RESETTING");
    Serial.flush();
    delay(50);
    ESP.restart();
  }

  if (strncmp(linha, "START ", 6) == 0) {
    if (transferenciaAtiva) { Serial.println("@FAIL ocupado"); return; }
    char *cmd = strtok(linha, " ");
    char *tipo = strtok(nullptr, " ");
    char *totalTexto = strtok(nullptr, " ");
    char *modo = strtok(nullptr, " ");
    char *blocoTexto = strtok(nullptr, " ");
    char *timeoutTexto = strtok(nullptr, " ");
    if (!cmd || !tipo || !totalTexto || !modo || !blocoTexto || !timeoutTexto ||
        strtok(nullptr, " ") || strlen(tipo) != 1 ||
        (tipo[0] != 'T' && tipo[0] != 'I') ||
        (strcmp(modo, "SW") != 0 && strcmp(modo, "SR") != 0)) {
      Serial.println("@FAIL start_invalido");
      return;
    }
    char *fim = nullptr;
    const unsigned long total = strtoul(totalTexto, &fim, 10);
    if (fim == totalTexto || *fim != '\0' || total > MAX_BYTES) {
      Serial.println("@FAIL tamanho_invalido");
      return;
    }
    const unsigned long bloco = strtoul(blocoTexto, &fim, 10);
    if (fim == blocoTexto || *fim != '\0' || bloco < 1 || bloco > 16) {
      Serial.println("@FAIL bloco_invalido");
      return;
    }
    const unsigned long timeout = strtoul(timeoutTexto, &fim, 10);
    if (fim == timeoutTexto || *fim != '\0' || timeout < 500 || timeout > 10000) {
      Serial.println("@FAIL timeout_invalido");
      return;
    }
    tamanho = total;
    carregados = 0;
    tamanhoQuadro = bloco;
    ackTimeoutMs = timeout;
    tipoTransferencia = tipo[0];
    seletivo = strcmp(modo, "SR") == 0;
    transferenciaAtiva = true;
    if (tamanho == 0) {
      transmitirArquivo();
      transferenciaAtiva = false;
    } else Serial.printf("@NEXT %lu\n", static_cast<unsigned long>(tamanho));
    return;
  }

  if (strncmp(linha, "CHUNK ", 6) == 0) {
    if (!transferenciaAtiva) { Serial.println("@FAIL sem_start"); return; }
    const size_t nHex = strlen(linha + 6);
    const size_t n = nHex / 2;
    if (nHex == 0 || (nHex & 1) || n > Rf433::MAX_DATA ||
        n > tamanho - carregados) {
      Serial.println("@FAIL chunk_invalido");
      transferenciaAtiva = false;
      return;
    }
    for (size_t i = 0; i < n; ++i) {
      const int8_t a = hexValor(linha[6 + 2 * i]);
      const int8_t b = hexValor(linha[7 + 2 * i]);
      if (a < 0 || b < 0) {
        Serial.println("@FAIL hex_invalido");
        transferenciaAtiva = false;
        return;
      }
      arquivo[carregados + i] = static_cast<uint8_t>((a << 4) | b);
    }
    carregados += n;
    if (carregados == tamanho) {
      transmitirArquivo();
      transferenciaAtiva = false;
    } else Serial.printf("@NEXT %lu\n",
                       static_cast<unsigned long>(tamanho - carregados));
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
