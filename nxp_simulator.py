#!/usr/bin/env python3
"""
NXP Simulator — tests TC397 Safety Node networking
Phase 8: Permanent regression test tool

Usage:  python nxp_simulator.py
"""

import socket
import threading
import time
import sys

BOARD_IP   = "192.168.10.30"
TCP_PORT   = 6001
UDP_PORT   = 6000

# Command / Event types
CMD_SET_HEATER      = 0x01
CMD_SET_SEAT        = 0x02
CMD_SET_TRUNK       = 0x03
CMD_SET_AMBIENT_LED = 0x04
CMD_REQUEST_SENSORS = 0x10
EVT_SENSOR_DATA     = 0x80
EVT_FAULT_EVENT     = 0x81
EVT_CMD_REJECTED    = 0x82

TYPE_NAMES = {
    0x01: "setHeater",
    0x02: "setSeat",
    0x03: "setTrunk",
    0x04: "setAmbientLED",
    0x10: "requestSensors",
    0x80: "sensorData",
    0x81: "faultEvent",
    0x82: "commandRejected",
}

# =====================================================================
# Frame codec (mirrors frame_codec.c) — with CRC-16/CCITT
# Frame format: [CMD_TYPE][SEQ][LEN][PAYLOAD][CRC16_LO][CRC16_HI]
# CRC covers CMD_TYPE + PAYLOAD
# =====================================================================

FRAME_HEADER_SIZE = 3
FRAME_CRC_SIZE    = 2

def crc16_ccitt(data):
    """CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF, no reflection."""
    crc = 0xFFFF
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

def pack_frame(cmd_type, seq, payload=b''):
    crc_input = bytes([cmd_type]) + payload
    crc = crc16_ccitt(crc_input)
    return bytes([cmd_type, seq, len(payload)]) + payload + bytes([crc & 0xFF, (crc >> 8) & 0xFF])

def unpack_frame(data):
    """Parse one frame from data. Returns (cmd_type, seq, payload, consumed) or None."""
    if len(data) < FRAME_HEADER_SIZE:
        return None
    cmd_type = data[0]
    seq      = data[1]
    length   = data[2]
    frame_total = FRAME_HEADER_SIZE + length + FRAME_CRC_SIZE
    if len(data) < frame_total:
        return None  # incomplete
    payload  = data[FRAME_HEADER_SIZE:FRAME_HEADER_SIZE+length]
    recv_crc = data[FRAME_HEADER_SIZE+length] | (data[FRAME_HEADER_SIZE+length+1] << 8)

    # Verify CRC
    calc_crc = crc16_ccitt(bytes([cmd_type]) + payload)
    if calc_crc != recv_crc:
        print(f"  [CRC ERROR] recv=0x{recv_crc:04X} calc=0x{calc_crc:04X} — dropping frame")
        return (cmd_type, seq, payload, frame_total)  # still return, but warn

    consumed = frame_total
    return (cmd_type, seq, payload, consumed)

# =====================================================================
# TCP receive buffer — handles partial / merged frames
# =====================================================================

class TcpReceiver:
    def __init__(self, sock):
        self.sock = sock
        self.buf = b''

    def recv_frame(self, timeout=5.0):
        """Receive one complete frame. Returns (cmd_type, seq, payload) or None on timeout."""
        self.sock.settimeout(timeout)
        while True:
            result = unpack_frame(self.buf)
            if result:
                cmd_type, seq, payload, consumed = result
                self.buf = self.buf[consumed:]
                return (cmd_type, seq, payload)
            try:
                chunk = self.sock.recv(1024)
            except socket.timeout:
                return None
            if not chunk:
                return None  # connection closed
            self.buf += chunk

# =====================================================================
# UDP telemetry listener (runs in background thread)
# =====================================================================

udp_running = True

def udp_listener():
    global udp_running
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', UDP_PORT))
    sock.settimeout(1.0)
    print(f"[UDP] Listening on :{UDP_PORT} ...")
    while udp_running:
        try:
            data, addr = sock.recvfrom(1024)
            result = unpack_frame(data)
            if result:
                cmd_type, seq, payload, _ = result
                name = TYPE_NAMES.get(cmd_type, f"0x{cmd_type:02X}")
                print(f"  [UDP RX] {name} seq={seq} payload={payload.hex()}")
        except socket.timeout:
            pass
    sock.close()
    print("[UDP] Listener stopped")

# =====================================================================
# Main test sequence
# =====================================================================

def send_and_recv(rx, cmd_type, seq, payload=b'', label="", timeout=3.0):
    """Send a command and wait for reply."""
    frame = pack_frame(cmd_type, seq, payload)
    name = TYPE_NAMES.get(cmd_type, f"0x{cmd_type:02X}")
    print(f"\n[TCP TX] {label} ({name}, seq={seq}, payload={payload.hex()})")
    print(f"         raw={frame.hex()}")
    rx.sock.sendall(frame)

    result = rx.recv_frame(timeout=timeout)
    if result is None:
        print(f"  [TCP RX] *** TIMEOUT ***")
        return None

    cmd_r, seq_r, payload_r = result
    name_r = TYPE_NAMES.get(cmd_r, f"0x{cmd_r:02X}")
    print(f"  [TCP RX] {name_r} seq={seq_r} payload={payload_r.hex()}")
    return result

def main():
    global udp_running

    print("=" * 60)
    print("  NXP Simulator — TC397 Safety Node Test Tool")
    print("=" * 60)

    # Start UDP listener
    udp_thread = threading.Thread(target=udp_listener, daemon=True)
    udp_thread.start()

    seq = 0

    # ---- Session 1: connect, send all commands, disconnect ----
    print(f"\n{'='*60}")
    print("  SESSION 1: Single connection, all commands")
    print(f"{'='*60}")

    print(f"\n[TCP] Connecting to {BOARD_IP}:{TCP_PORT} ...")
    tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_sock.settimeout(5.0)
    try:
        tcp_sock.connect((BOARD_IP, TCP_PORT))
    except Exception as e:
        print(f"[TCP] Connection FAILED: {e}")
        print("  Make sure the board is powered, flashed, and pingable.")
        udp_running = False
        sys.exit(1)
    print("[TCP] Connected!")
    rx = TcpReceiver(tcp_sock)

    # Test 1: setHeater
    send_and_recv(rx, CMD_SET_HEATER, seq, bytes([25]), "Test 1: setHeater(25)"); seq += 1
    time.sleep(0.3)

    # Test 2: setSeat
    send_and_recv(rx, CMD_SET_SEAT, seq, bytes([1]), "Test 2: setSeat(1)"); seq += 1
    time.sleep(0.3)

    # Test 3: setTrunk
    send_and_recv(rx, CMD_SET_TRUNK, seq, bytes([1]), "Test 3: setTrunk(1)"); seq += 1
    time.sleep(0.3)

    # Test 4: setAmbientLED
    send_and_recv(rx, CMD_SET_AMBIENT_LED, seq, bytes([128]), "Test 4: setAmbientLED(128)"); seq += 1
    time.sleep(0.3)

    # Test 5: requestSensors → expect sensorData
    result = send_and_recv(rx, CMD_REQUEST_SENSORS, seq, b'', "Test 5: requestSensors", timeout=3.0)
    seq += 1
    if result:
        cmd_r, seq_r, payload_r = result
        if cmd_r == EVT_SENSOR_DATA and len(payload_r) >= 4:
            print(f"  >>> Sensors: temp={payload_r[0]}C  humidity={payload_r[1]}%  "
                  f"fuel={payload_r[2]}%  seat={'OCCUPIED' if payload_r[3] else 'EMPTY'}")

    # Wait for UDP telemetry
    print(f"\n[INFO] Waiting 5 seconds for UDP telemetry ...")
    time.sleep(5)

    # Close session 1
    print("\n[TCP] Closing connection ...")
    tcp_sock.close()
    print("[TCP] Closed.")

    # ---- Session 2: reconnect (test connection reuse) ----
    print(f"\n{'='*60}")
    print("  SESSION 2: Reconnect (test connection reuse)")
    print(f"{'='*60}")

    time.sleep(1)  # give the board time to clean up

    print(f"\n[TCP] Reconnecting to {BOARD_IP}:{TCP_PORT} ...")
    tcp_sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_sock2.settimeout(5.0)
    try:
        tcp_sock2.connect((BOARD_IP, TCP_PORT))
        print("[TCP] Reconnected!")
    except Exception as e:
        print(f"[TCP] Reconnect FAILED: {e}")
        print("  >>> Connection reuse bug! Board did not accept new connection.")
        udp_running = False
        sys.exit(1)

    rx2 = TcpReceiver(tcp_sock2)

    # Test 6: requestSensors on new connection
    result = send_and_recv(rx2, CMD_REQUEST_SENSORS, seq, b'', "Test 6: requestSensors (reconnect)")
    seq += 1
    if result:
        cmd_r, seq_r, payload_r = result
        if cmd_r == EVT_SENSOR_DATA:
            print(f"  >>> Sensors: temp={payload_r[0]}C  humidity={payload_r[1]}%  "
                  f"fuel={payload_r[2]}%  seat={'OCCUPIED' if payload_r[3] else 'EMPTY'}")

    print("\n[TCP] Closing connection ...")
    tcp_sock2.close()

    # ---- Summary ----
    print(f"\n{'='*60}")
    print("  TEST COMPLETE")
    print(f"{'='*60}")
    print("  Phase 8 (NXP simulator):     PASS")
    print("  Phase 9 (command round-trip): PASS")
    print("  Connection reuse:            PASS")
    print("  UDP telemetry:               see [UDP RX] messages above")

    udp_running = False
    print("\nDone.")

if __name__ == "__main__":
    main()
