import serial
import logging

PORT = 'COM4'
BAUDRATE = 115200
SYNC_BYTE = 0xAA
LOG_FILE = 'pico_events.log'

SEVERITY_NAMES = {0: "DEBUG", 1: "INFO", 2: "WARN", 3: "ERROR"}
SEVERITY_LEVELS = {0: logging.DEBUG, 1: logging.INFO, 2: logging.WARNING, 3: logging.ERROR}

logger = logging.getLogger("pico")
logger.setLevel(logging.DEBUG)

console_handler = logging.StreamHandler()
console_handler.setFormatter(logging.Formatter('%(message)s'))
logger.addHandler(console_handler)

file_handler = logging.FileHandler(LOG_FILE, mode='a', encoding='utf-8')
file_handler.setFormatter(logging.Formatter('%(asctime)s - %(levelname)s - %(message)s'))
logger.addHandler(file_handler)

def crc16ccitt(data, crc=0xFFFF):
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

def main():
    ser = serial.Serial(PORT, BAUDRATE, timeout=1)
    logger.info(f"Listening on {PORT} at {BAUDRATE} baud...")
    
    while True:
        b = ser.read(1)
        if not b or b[0] != SYNC_BYTE:
            continue
        length_bytes = ser.read(1)
        if not length_bytes:
            continue
        length = length_bytes[0]
        body = ser.read(length)
        crc_bytes = ser.read(2)
        if len(body) != length or len(crc_bytes) != 2:
            logger.warning(f"Incomplete frame: length={length}, body_got={len(body)}/{length}, crc_got={len(crc_bytes)}/2")
            continue

        received_crc = crc_bytes[0] | (crc_bytes[1] << 8)
        if crc16ccitt(body) != received_crc:
            logger.warning(f"CRC MISMATCH, dropping frame: {body.hex()} with CRC {received_crc:04X}")
            continue

        ts = body[0] | (body[1] << 8) | (body[2] << 16) | (body[3] << 24)
        severity = body[4]
        msg = body[5:].decode('utf-8', errors='replace')
        severity_name = SEVERITY_NAMES.get(severity, "UNKNOWN")
        level = SEVERITY_LEVELS.get(severity, logging.INFO)

        logger.log(level, f"[pico {ts:>8}ms] {severity_name:5} {msg}")


if __name__ == "__main__":
    main()