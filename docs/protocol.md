# Reliable Data Transfer Protocol

## 1. Packet Format

All three reliability protocols use the same common packet format.

| Field | Size | Purpose |
|---|---:|---|
| type | 1 byte | Identifies the packet type, such as DATA or ACK |
| flags | 1 byte | Protocol control information |
| sequence_number | 4 bytes | Identifies a DATA packet |
| acknowledgement_number | 4 bytes | Identifies the packet being acknowledged |
| payload_length | 2 bytes | Number of valid bytes in the payload |
| checksum | 2 bytes | Detects corrupted packets |
| payload | 1024 bytes max | Actual file/data being transferred |

## 2. Packet Types

The initial implementation uses two packet types:

- DATA
- ACK

Additional packet types such as START and END may be added later if required.

### DATA

A DATA packet carries a portion of the file/data.

It contains:
- sequence number
- payload length
- checksum
- payload

### ACK

An ACK packet confirms successful reception of a DATA packet.

It contains:
- acknowledgement number
- checksum

## 3. Sequence Number Rules

Sequence numbers identify DATA packets.

- Sequence numbers start from 0.
- Each DATA packet receives the next sequence number.
- The sequence number increases by 1 for each new DATA packet.
- Retransmitted packets keep their original sequence number.
- ACK packets use the acknowledgement number to identify the DATA packet being acknowledged.

Example:

DATA seq=0
DATA seq=1
DATA seq=2
DATA seq=3

## 4. ACK Rules

### Stop-and-Wait

Each DATA packet is acknowledged individually.

Example:

DATA seq=0
        ↓
ACK 0

### Go-Back-N

ACKs are cumulative.

For example, if packets 0, 1, 2 and 3 have been received correctly:

ACK 3

means that packets through sequence number 3 have been received correctly.

### Selective Repeat

ACKs are individual.

Example:

DATA seq=0
DATA seq=1
DATA seq=2

If packets 0 and 2 are received:

ACK 0
ACK 2

Packet 1 can be retransmitted separately.

## 5. Checksum

The checksum is used to detect corrupted packets.

The sender calculates the checksum before transmission.

The receiver recalculates the checksum after receiving a packet.

If the values match, the packet is considered valid.

If they do not match, the packet is considered corrupted and is not accepted as valid data.

## 6. Reliability Protocols

The same packet format will support:

1. Stop-and-Wait
2. Go-Back-N
3. Selective Repeat

The reliability mechanism is determined by the sender/receiver logic rather than by creating a completely different packet format.

## Sequence Number Rules

The `sequence_number` field identifies the packet number.

For data packets:

- The first packet starts with sequence number 0.
- Each subsequent data packet increments the sequence number by 1.
- Sequence numbers are therefore 0, 1, 2, 3, ...

Example:

| Packet | Sequence Number |
|--------|-----------------|
| Packet 0 | 0 |
| Packet 1 | 1 |
| Packet 2 | 2 |
| Packet 3 | 3 |

The same sequence-number scheme will be used by Stop-and-Wait,
Go-Back-N, and Selective Repeat.

## Maximum Payload Size

The maximum payload size is 1024 bytes.

A file is divided into chunks of at most 1024 bytes.
Each chunk is carried as the payload of one UDP packet.

For example:

File
↓
Split into 1024-byte chunks
↓
UDP packets

The final chunk may contain fewer than 1024 bytes.

This limit keeps application-level packets reasonably sized
and allows large files to be transferred as multiple packets.

## Byte Order

All multi-byte integer fields in the packet header use network byte order
(big-endian).

The following functions will be used when serializing and deserializing
packet fields:

- `htonl()` — host to network order for 32-bit integers
- `ntohl()` — network to host order for 32-bit integers
- `htons()` — host to network order for 16-bit integers
- `ntohs()` — network to host order for 16-bit integers

For example, the `sequence_number` and `acknowledgement_number` fields
are 32-bit integers and will use `htonl()` before transmission and
`ntohl()` after reception.

The `payload_length` and `checksum` fields are 16-bit integers and will
use `htons()` before transmission and `ntohs()` after reception.

This ensures that the packet format is interpreted consistently
regardless of the host machine's byte order.

## Wire Format

The C `Packet` structure is not sent directly using `sendto()` because
C structures may contain padding and alignment differences.

The packet is serialized into a byte buffer before transmission.

### Header Layout

| Field | Size |
|---|---:|
| Type | 1 byte |
| Flags | 1 byte |
| Sequence Number | 4 bytes |
| Acknowledgement Number | 4 bytes |
| Payload Length | 2 bytes |
| Checksum | 2 bytes |
| Payload | Variable |

### Wire Format

```text
+-----------------------------+
| Type          | 1 byte      |
+-----------------------------+
| Flags         | 1 byte      |
+-----------------------------+
| Sequence No.  | 4 bytes     |
+-----------------------------+
| ACK No.       | 4 bytes     |
+-----------------------------+
| Payload Len.  | 2 bytes     |
+-----------------------------+
| Checksum      | 2 bytes     |
+-----------------------------+
| Payload       | Variable    |
+-----------------------------+

## Sender Flow — Week 1

The Week 1 sender performs basic file transfer over UDP.

```text
START
  |
  v
Open input file
  |
  v
Create UDP socket
  |
  v
Read up to MAX_PAYLOAD_SIZE bytes
  |
  v
Construct DATA packet
  |
  v
Serialize packet into wire format
  |
  v
Send UDP packet
  |
  v
More file data?
  |              |
 Yes             No
  |              |
  v              v
Read next       Finish
 chunk
  |
  +----> Construct DATA packet
  
## Receiver Flow — Week 1

The Week 1 receiver performs basic file reception over UDP.

1. Create a UDP socket.
2. Bind the socket to the receiver port.
3. Wait for an incoming UDP packet.
4. Receive the UDP datagram.
5. Parse the packet according to the defined wire format.
6. Verify the packet type and checksum.
7. Extract the payload.
8. Write the payload to the output file.
9. Wait for the next packet.
10. Continue until the file transfer is complete.

There is no ACK, timeout, retransmission, or other reliability
mechanism implemented in Week 1.