#!/usr/bin/env python3

"""
flash_update.py

Pushes a patched application .bin into the bootloader over UART,
using the framed protocol defined in update_protocol.c:

    SOF(1) CMD(1) LEN(2, LE) PAYLOAD(LEN) CRC16(2, LE)
"""

import sys
import struct
import serial


SOF = 0xA5

CMD_START_UPDATE = 0x01
CMD_DATA = 0x02
CMD_END_UPDATE = 0x03

CMD_ACK = 0x10
CMD_NACK = 0x11

CHUNK_SIZE = 256
RETRIES = 3


NACK_MESSAGES = {
    0x01: "invalid slot",
    0x02: "flash write failed",
    0x03: "CRC mismatch",
    0x04: "overflow (image too large for slot)",
}


SLOT_NAMES = {
    1: "default_app",
    2: "app1",
}


def crc16(data):
    """Calculate CRC16-IBM/Modbus."""

    crc = 0xFFFF

    for byte in data:
        crc ^= byte

        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1

    return crc & 0xFFFF


def build_frame(cmd, payload=b""):
    """
    Build:

        SOF | CMD | LEN | PAYLOAD | CRC16
    """

    header = bytes([cmd]) + struct.pack("<H", len(payload))

    crc = crc16(header + payload)

    return (
        bytes([SOF])
        + header
        + payload
        + struct.pack("<H", crc)
    )


def send_frame(ser, cmd, payload=b""):
    """Send one complete protocol frame."""

    ser.write(build_frame(cmd, payload))


def read_response(ser, timeout_s=2.0):
    """
    Read one response frame.

    Returns:
        (cmd, payload)

    or:

        (None, None)

    if the response is incomplete or invalid.
    """

    ser.timeout = timeout_s

    # Read SOF
    sof = ser.read(1)

    if len(sof) != 1 or sof[0] != SOF:
        return None, None

    # Read CMD + LEN
    header = ser.read(3)

    if len(header) != 3:
        return None, None

    cmd = header[0]

    length = struct.unpack("<H", header[1:3])[0]

    # Read payload
    payload = ser.read(length)

    if len(payload) != length:
        return None, None

    # Read CRC16
    crc = ser.read(2)

    if len(crc) != 2:
        return None, None

    return cmd, payload


def send_with_retry(ser, cmd, payload=b"", retries=RETRIES):
    """
    Send a frame and wait for an ACK.

    If there is no valid response, retransmit the frame up to
    'retries' times.

    A NACK is treated as a real protocol error and is not retried.

    Returns:
        True  -> ACK received
        False -> NACK received or all attempts timed out
    """

    for attempt in range(1, retries + 1):

        send_frame(ser, cmd, payload)

        cmd_resp, payload_resp = read_response(ser)

        # ACK
        if cmd_resp == CMD_ACK:
            return True

        # NACK
        if cmd_resp == CMD_NACK:

            err = payload_resp[0] if payload_resp else 0xFF

            print(
                f"  NACK: "
                f"{NACK_MESSAGES.get(err, f'unknown error 0x{err:02X}')}"
            )

            return False

        # No response
        if attempt < retries:
            print(
                f"  attempt {attempt}/{retries}: "
                f"no response, retrying..."
            )
        else:
            print(
                f"  attempt {attempt}/{retries}: "
                f"no response."
            )

    return False


def run_update(port, baud, slot_id, bin_path):

    # Read firmware image
    with open(bin_path, "rb") as f:
        image = f.read()

    print(
        f"Image: {bin_path} "
        f"({len(image)} bytes) "
        f"-> slot {slot_id} "
        f"({SLOT_NAMES.get(slot_id, '?')})"
    )

    # Open UART
    with serial.Serial(port, baud, timeout=2.0) as ser:

        # --------------------------------------------------------------
        # START_UPDATE
        # --------------------------------------------------------------

        print("Sending START_UPDATE...")

        if not send_with_retry(
            ser,
            CMD_START_UPDATE,
            bytes([slot_id])
        ):
            sys.exit(
                "update aborted: "
                "START_UPDATE not acknowledged after retries"
            )

        # --------------------------------------------------------------
        # DATA
        # --------------------------------------------------------------

        total_chunks = (
            len(image) + CHUNK_SIZE - 1
        ) // CHUNK_SIZE

        for i in range(0, len(image), CHUNK_SIZE):

            chunk = image[i:i + CHUNK_SIZE]

            chunk_num = (i // CHUNK_SIZE) + 1

            print(
                f"Sending chunk "
                f"{chunk_num}/{total_chunks} "
                f"({len(chunk)} bytes)..."
            )

            if not send_with_retry(
                ser,
                CMD_DATA,
                chunk
            ):
                sys.exit(
                    f"update aborted: "
                    f"chunk {chunk_num} "
                    f"not acknowledged after retries"
                )

        # --------------------------------------------------------------
        # END_UPDATE
        # --------------------------------------------------------------

        print("Sending END_UPDATE...")

        if not send_with_retry(
            ser,
            CMD_END_UPDATE
        ):
            sys.exit(
                "update aborted: "
                "END_UPDATE not acknowledged "
                "(CRC check likely failed)"
            )

        # --------------------------------------------------------------
        # SUCCESS
        # --------------------------------------------------------------

        print("Update successful.")


if __name__ == "__main__":

    if len(sys.argv) != 5:
        sys.exit(
            "usage: "
            "flash_update.py <port> <baud> "
            "<slot_id:1|2> <bin_path>"
        )

    port = sys.argv[1]

    baud = int(sys.argv[2])

    slot_id = int(sys.argv[3])

    bin_path = sys.argv[4]

    run_update(
        port,
        baud,
        slot_id,
        bin_path
    )