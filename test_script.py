#!/usr/bin/env python3
import serial
import threading
import argparse
import re
import time

addresses = [
    "d8:3b:da:6d:90:c9",
    "d8:3b:da:6d:eb:09",
    "d8:3b:da:6d:eb:35",
    "d8:3b:da:6e:ee:9d",
]

# Global dictionary to hold statistics for each client (client_id -> {'last_seq': int, 'dropped': int})
client_stats = {}
stats_lock = threading.Lock()


def write_and_print(ser, message):
    ser.write(message.encode("utf-8"))
    print(ser.port + " > " + message.strip())


def read_and_print(ser):
    while True:
        line = ser.readline().decode("utf-8").strip()
        print(ser.port + " < " + line)
        if line == "OK" or line == "":
            break


# Regular expression to capture notification lines.
# Expected format example:
#   Notification received from client 2 (hex): 02 FF FF 00 00 0B ...
notification_pattern = re.compile(
    r"Notification received from client (\d+)\s+\(hex\):\s*(.*)"
)


def process_line(line, port_name):
    """Process a line of text from the serial port and update packet drop count."""
    line = line.strip()
    match = notification_pattern.search(line)
    if match:
        client_id = int(match.group(1))
        hex_data_str = match.group(2)
        # Split the hex bytes (assumed to be space-separated)
        hex_bytes = [b for b in hex_data_str.split() if b]
        # Now expecting at least 6 bytes:
        # Byte 0: client number, Bytes 1-2: default header, Bytes 3-5: 3-byte sequence number
        if len(hex_bytes) >= 6:
            try:
                # Extract the sequence number from bytes 3 to 5 (big-endian)
                seq_hex = "".join(hex_bytes[3:6])
                seq_num = int(seq_hex, 16)
            except Exception as e:
                print(f"[{port_name}] Error parsing sequence number: {e}")
                return

            with stats_lock:
                if client_id not in client_stats:
                    # First packet from this client
                    client_stats[client_id] = {"last_seq": seq_num, "dropped": 0}
                    print(
                        f"[{port_name}] Client {client_id}: First packet with sequence {seq_num}."
                    )
                else:
                    last_seq = client_stats[client_id]["last_seq"]
                    expected_seq = last_seq + 1
                    if seq_num != expected_seq:
                        # Calculate how many packets were dropped
                        dropped = (
                            (seq_num - expected_seq) if seq_num > expected_seq else 0
                        )
                        client_stats[client_id]["dropped"] += dropped
                        print(
                            f"[{port_name}] Client {client_id}: Detected {dropped} dropped packets (last: {last_seq}, current: {seq_num})."
                        )
                    else:
                        print(
                            f"[{port_name}] Client {client_id}: Packet received with sequence {seq_num}."
                        )
                    # Update last sequence number for this client
                    client_stats[client_id]["last_seq"] = seq_num
        else:
            print(
                f"[{port_name}] Client {match.group(1)}: Insufficient data to extract sequence: {hex_bytes}"
            )
    else:
        # For lines that do not match the notification pattern
        print(f"[{port_name}] {line}")


def serial_thread(port_name, baudrate, address_subset):
    try:
        ser = serial.Serial(port=port_name, baudrate=baudrate, timeout=2)
        print(f"Opened serial port: {port_name}")
        ser.reset_input_buffer()
        ser.reset_output_buffer()
    except Exception as e:
        print(f"Failed to open port {port_name}: {e}")
        return

    # Configure BLE connections for each address in the subset
    for idx, address in enumerate(address_subset):
        write_and_print(ser, "AT+BLESTART\r\n")
        time.sleep(1)
        read_and_print(ser)
        write_and_print(ser, f"AT+BLECONNECT={address}\r\n")
        time.sleep(1)
        read_and_print(ser)
        write_and_print(
            ser, f"AT+BLESETSERVICE={idx+1},4fafc201-1fb5-459e-8fcc-c5c9c331914b\r\n"
        )
        time.sleep(1)
        read_and_print(ser)
        write_and_print(
            ser, f"AT+BLESETCHAR={idx+1},beb5483e-36e1-4688-b7f5-ea07361b26a8\r\n"
        )
        time.sleep(2)
        read_and_print(ser)
        time.sleep(1)
    time.sleep(1)
    write_and_print(ser, "AT+BLENOTIFY=1\r\n")
    time.sleep(1)
    write_and_print(ser, "AT+BLENOTIFY=2\r\n")
    time.sleep(1)

    # Continuously read notifications and process them
    while True:
        try:
            line = ser.readline().decode("utf-8").strip()
            if line:
                process_line(line, port_name)
            else:
                time.sleep(0.1)
        except Exception as e:
            print(f"[{port_name}] Error reading from serial: {e}")
            break


def main():
    parser = argparse.ArgumentParser(description="BLE AT Firmware Packet Drop Counter")
    parser.add_argument(
        "--ports",
        nargs="+",
        required=True,
        help="List of serial port names, e.g., /dev/ttyUSB0 /dev/ttyUSB1",
    )
    parser.add_argument(
        "--baudrate",
        type=int,
        default=921600,
        help="Baud rate for serial communication (default: 921600)",
    )
    args = parser.parse_args()

    threads = []
    port_num = len(args.ports)

    for idx, port in enumerate(args.ports):
        t = threading.Thread(
            target=serial_thread,
            args=(
                port,
                args.baudrate,
                addresses[idx * port_num : (idx + 1) * port_num],
            ),
        )
        t.daemon = True
        t.start()
        threads.append(t)

    print("Monitoring serial ports for BLE notifications...")
    try:
        while True:
            time.sleep(1)
            # Print a summary of dropped packets for each client every second
            with stats_lock:
                for client_id, stats in client_stats.items():
                    print(
                        f"Summary - Client {client_id}: Dropped Packets: {stats['dropped']}"
                    )
    except KeyboardInterrupt:
        print("Exiting monitoring.")


if __name__ == "__main__":
    main()
