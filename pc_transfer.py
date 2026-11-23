"""Envia texto/imagem entre dois PCs via ESP32 e RF433.

Instale pyserial: py -m pip install pyserial
Execute `py pc_transfer.py --help` para ver os comandos.
"""

import argparse
from pathlib import Path
import time


MAX_BYTES = 16_384


def select_port(requested):
    from serial.tools import list_ports

    if requested:
        return requested
    ports = list(list_ports.comports())
    usb = [port for port in ports if port.vid is not None]
    candidates = usb if usb else ports
    if len(candidates) == 1:
        print(f"ESP32/USB selecionado: {candidates[0].device}")
        return candidates[0].device
    available = ", ".join(f"{port.device} ({port.description})" for port in ports)
    raise RuntimeError(
        "Não foi possível escolher a porta automaticamente. "
        f"Portas encontradas: {available or 'nenhuma'}. Use --port PORTA."
    )


def read_event(port, deadline):
    while time.monotonic() < deadline:
        line = port.readline().decode("ascii", errors="replace").strip()
        if not line:
            continue
        if line.startswith("@"):
            return line
        print(f"ESP32: {line}")
    raise TimeoutError("O ESP32 não respondeu; confira a porta e as ligações RF.")


def await_sender(port):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        port.write(b"HELLO\n")
        try:
            if read_event(port, min(deadline, time.monotonic() + 2)) == "@READY":
                return
        except TimeoutError:
            pass
    raise TimeoutError("O transmissor não respondeu a HELLO.")


def send(args):
    import serial

    kind = "T" if args.text is not None else "I"
    payload = args.text.encode("utf-8") if kind == "T" else Path(args.image).read_bytes()
    if len(payload) > MAX_BYTES:
        raise ValueError(f"Arquivo com {len(payload)} bytes; limite atual: {MAX_BYTES} bytes.")

    port_name = select_port(args.port)
    with serial.Serial(port_name, 115200, timeout=0.5, write_timeout=5) as port:
        time.sleep(2)  # A abertura da porta pode reiniciar o ESP32.
        port.reset_input_buffer()
        await_sender(port)
        port.write(f"START {kind} {len(payload)}\n".encode("ascii"))
        sent = 0
        while True:
            event = read_event(port, time.monotonic() + 30)
            if event.startswith("@FAIL"):
                raise RuntimeError(f"Transmissão falhou: {event}. Reinicie os dois ESP32.")
            if event == "@DONE":
                if sent != len(payload):
                    raise RuntimeError("Transmissor encerrou antes de enviar todos os bytes.")
                print(f"Enviado: {sent} bytes; END confirmado por ACK.")
                return
            if event.startswith("@NEXT "):
                remaining = int(event.split()[1])
                if remaining != len(payload) - sent or remaining <= 0:
                    raise RuntimeError(f"Contagem inesperada do transmissor: {event}")
                chunk = payload[sent:sent + args.chunk]
                port.write(b"CHUNK " + chunk.hex().upper().encode("ascii") + b"\n")
                sent += len(chunk)
                print(f"Enviando {sent}/{len(payload)} bytes", flush=True)


def image_extension(data):
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        return ".png"
    if data.startswith(b"\xff\xd8\xff"):
        return ".jpg"
    if data.startswith((b"GIF87a", b"GIF89a")):
        return ".gif"
    if data.startswith(b"BM"):
        return ".bmp"
    return ".bin"


def receive(args):
    import serial

    kind = None
    expected = 0
    data = bytearray()
    port_name = select_port(args.port)
    with serial.Serial(port_name, 115200, timeout=0.5) as port:
        print(f"Aguardando no {port_name}. Inicie o envio no outro PC. Ctrl+C encerra.")
        while True:
            line = port.readline().decode("ascii", errors="replace").strip()
            if not line:
                continue
            if line.startswith("@START "):
                parts = line.split()
                if len(parts) != 3 or parts[1] not in ("T", "I"):
                    raise RuntimeError(f"Cabeçalho serial inválido: {line}")
                kind, expected = parts[1], int(parts[2])
                if not 0 <= expected <= MAX_BYTES:
                    raise RuntimeError(f"Tamanho inválido: {expected}")
                data.clear()
                print(f"Recebendo {'texto' if kind == 'T' else 'imagem'}: {expected} bytes")
            elif line.startswith("@DATA "):
                if kind is None:
                    raise RuntimeError("Dados recebidos antes de START")
                data.extend(bytes.fromhex(line[6:]))
                if len(data) > expected:
                    raise RuntimeError("Recebidos mais bytes que o tamanho informado")
                print(f"Recebidos {len(data)}/{expected} bytes", flush=True)
            elif line == "@END":
                if kind is None or len(data) != expected:
                    raise RuntimeError(f"END incompleto: {len(data)}/{expected} bytes")
                if kind == "T":
                    result = data.decode("utf-8")
                    print(f"Texto recebido: {result}")
                    if args.output:
                        Path(args.output).write_text(result, encoding="utf-8")
                else:
                    path = Path(args.output) if args.output else Path("imagem_recebida" + image_extension(data))
                    path.write_bytes(data)
                    print(f"Imagem salva em: {path.resolve()}")
                kind = None
                expected = 0
                data.clear()
                if args.once:
                    return
            elif line.startswith("@"):
                print(f"Receptor: {line}")
            else:
                print(f"Receptor: {line}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sender = sub.add_parser("send", help="Envia texto ou arquivo de imagem")
    sender.add_argument("--port", help="Opcional; ex.: COM3. Detecta se houver uma única USB.")
    source = sender.add_mutually_exclusive_group(required=True)
    source.add_argument("--text", help="Texto UTF-8")
    source.add_argument("--image", help="Arquivo de imagem (até 16 KiB)")
    sender.add_argument("--chunk", type=int, default=8, help="Bytes por quadro (1–16; padrão: 8)")
    receiver = sub.add_parser("receive", help="Reconstrói texto/imagem no PC receptor")
    receiver.add_argument("--port", help="Opcional; ex.: COM6. Detecta se houver uma única USB.")
    receiver.add_argument("--output", help="Arquivo de saída; opcional para texto")
    receiver.add_argument("--once", action="store_true", help="Encerra após uma transferência")
    args = parser.parse_args()
    if args.command == "send":
        if not 1 <= args.chunk <= 16:
            parser.error("--chunk deve ficar entre 1 e 16")
        send(args)
    else:
        receive(args)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, TimeoutError) as exc:
        raise SystemExit(f"Erro: {exc}") from exc
