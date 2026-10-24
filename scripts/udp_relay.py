
import socket

LISTEN_IP = "127.0.0.1"
RELAY_PORT = 9001
RECEIVER_IP = "127.0.0.1"
RECEIVER_PORT = 9000

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((LISTEN_IP, RELAY_PORT))

receiver_address = (RECEIVER_IP, RECEIVER_PORT)
sender_address = None
drop_first_data = True

print(f"UDP relay listening on {LISTEN_IP}:{RELAY_PORT}")

while True:
    data, address = sock.recvfrom(2048)

    if address == receiver_address:
        if sender_address is not None:
            sock.sendto(data, sender_address)
            print("Forwarded receiver response to sender")
        continue

    sender_address = address

    # DATA packets have type 1 in the first byte.
    if data and data[0] == 1 and drop_first_data:
        drop_first_data = False
        print("TEST: Dropped first DATA packet")
        continue

    sock.sendto(data, receiver_address)
    print("Forwarded packet to receiver")
