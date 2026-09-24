# Reliable Data Transfer Protocol

## 1. Packet Format

The protocol uses a common packet format for DATA, ACK, and FIN packets.

| Field | Size | Meaning |
|---|---:|---|
| type | 1 byte | Identifies the packet type |
| flags | 1 byte | Protocol control information |
| sequence_number | 4 bytes | Identifies a DATA packet |
| acknowledgement_number | 4 bytes | Identifies the DATA packet being acknowledged |
| payload_length | 2 bytes | Number of valid bytes in the payload |
| checksum | 2 bytes | Detects corrupted packets |
| payload | 0–1024 bytes | Actual file/data being transferred |

The fixed header size is 14 bytes.

The maximum payload size is 1024 bytes.

The C Packet structure is not sent directly using sendto(). The packet is
explicitly serialized into a byte buffer so that structure padding, alignment,
and host byte order do not affect the wire format.

---

## 2. Packet Types

The protocol uses three packet types:

- DATA
- ACK
- FIN

### DATA

A DATA packet carries a portion of the file.

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

ACK packets are used by the reliability mechanisms that will be added later.

### FIN

A FIN packet indicates that the sender has finished transmitting the file.

It contains:

- sequence number
- checksum
- zero-byte payload

The receiver uses the FIN packet to determine that the file transfer has
finished.

---

## 3. Sequence Number Rules

The sequence_number field identifies each DATA packet.

The rules are:

- The first DATA packet starts with sequence number 0.
- Each new DATA packet increases the sequence number by 1.
- Sequence numbers are therefore 0, 1, 2, 3, ...
- A retransmitted DATA packet keeps its original sequence number.
- ACK packets use the acknowledgement_number field to identify the DATA
  packet being acknowledged.
- The FIN packet uses the next sequence number after the final DATA packet.

Example:

DATA seq=0
DATA seq=1
DATA seq=2
DATA seq=3
FIN

The same sequence-number scheme is designed to support Stop-and-Wait,
Go-Back-N, and Selective Repeat.

---

## 4. Payload Size

The maximum payload size is:

MAX_PAYLOAD_SIZE = 1024 bytes

The sender divides the input file into chunks of at most 1024 bytes.

For example, a 2500-byte file is divided as follows:

DATA packet 0 -> 1024 bytes
DATA packet 1 -> 1024 bytes
DATA packet 2 -> 452 bytes

The final DATA packet may contain fewer than 1024 bytes.

A dedicated FIN packet is used to signal the end of the transfer. Therefore,
the receiver does not need to assume that a short DATA packet is necessarily
the final packet.

The 1024-byte limit allows large files to be transferred as multiple packets
while keeping the application-level payload size fixed and predictable.

---

## 5. Byte Order

All multi-byte integer fields in the packet header use network byte order
(big-endian).

The following functions are used when serializing and deserializing fields:

- htonl() - host to network order for 32-bit integers
- ntohl() - network to host order for 32-bit integers
- htons() - host to network order for 16-bit integers
- ntohs() - network to host order for 16-bit integers

The sequence_number and acknowledgement_number fields are 32-bit integers.
They use htonl() before transmission and ntohl() after reception.

The payload_length and checksum fields are 16-bit integers. They use htons()
before transmission and ntohs() after reception.

Using network byte order ensures that the packet format is interpreted
consistently regardless of the host machine's byte order.

---

## 6. Transfer Termination

The protocol uses a dedicated FIN packet to signal the end of a file
transfer.

After all DATA packets have been transmitted, the sender sends a PACKET_FIN
packet with zero payload bytes.

For example, for a 2500-byte file:

DATA seq=0 -> 1024 bytes
DATA seq=1 -> 1024 bytes
DATA seq=2 -> 452 bytes
FIN        -> 0 bytes

The receiver processes DATA packets normally. When it receives and validates
a FIN packet, it knows that the sender has finished transmitting the file.

The receiver then:

1. Recognizes the transfer as complete.
2. Stops waiting for additional DATA packets.
3. Closes the output file.
4. Terminates the receive operation.

A dedicated FIN packet is used instead of relying on a short DATA packet
because a file may have a size that is exactly a multiple of 1024 bytes.

For example, a 2048-byte file contains:

DATA seq=0 -> 1024 bytes
DATA seq=1 -> 1024 bytes
FIN        -> 0 bytes

Without a FIN packet, the receiver would not be able to distinguish the end
of the transfer from a situation where more data may still arrive.

---

## 7. Future ARQ Compatibility

The packet format is designed to support three common reliability protocols:

1. Stop-and-Wait
2. Go-Back-N
3. Selective Repeat

The reliability mechanism is implemented through sender and receiver logic.
A completely different packet format is not required for each protocol.

### Stop-and-Wait

In Stop-and-Wait, the sender transmits one DATA packet and waits for its ACK
before sending the next DATA packet.

Example:

DATA seq=0
       |
       v
ACK 0
       |
       v
DATA seq=1
       |
       v
ACK 1

If the expected ACK is not received within the timeout period, the sender can
retransmit the DATA packet using the same sequence number.

### Go-Back-N

In Go-Back-N, the sender can have multiple unacknowledged DATA packets in
transmission.

ACKs can be cumulative.

For example:

DATA seq=0
DATA seq=1
DATA seq=2
DATA seq=3

An ACK identifying sequence number 3 can indicate that packets through
sequence number 3 have been received correctly, according to the protocol's
cumulative acknowledgement rule.

If a packet is lost, the sender can retransmit the missing packet and the
subsequent packets in the current transmission window.

### Selective Repeat

In Selective Repeat, individual DATA packets can be acknowledged separately.

For example:

DATA seq=0
DATA seq=1
DATA seq=2

If packets 0 and 2 are received correctly while packet 1 is missing:

ACK 0
ACK 2

The sender can retransmit packet 1 without retransmitting packets 0 and 2.

The sequence number and acknowledgement number fields provide the information
required to implement these reliability mechanisms.

---

## 8. Checksum

The checksum is used to detect corrupted packets.

The sender calculates the checksum before transmitting a packet.

The receiver recalculates the checksum after parsing the received packet.

If the calculated checksum matches the checksum stored in the packet, the
packet is considered valid.

If the values do not match, the packet is considered corrupted and its
payload is not accepted as valid file data.

Checksum verification is performed before writing the received payload to
the output file.

---

## 9. Wire Format

The packet is serialized into a byte buffer before being sent using UDP.

The C Packet structure is not transmitted directly because C structures
may contain padding and alignment differences between systems.

### Header Layout

| Field | Size |
|---|---:|
| Type | 1 byte |
| Flags | 1 byte |
| Sequence Number | 4 bytes |
| Acknowledgement Number | 4 bytes |
| Payload Length | 2 bytes |
| Checksum | 2 bytes |
| Payload | 0–1024 bytes |

### Wire Format

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
| Payload       | 0-1024 bytes|
+-----------------------------+

The first 14 bytes form the fixed header.

The payload follows the header and contains only the number of bytes
specified by payload_length.

---

## 10. Error Handling

The protocol defines how common errors are handled.

### Invalid Packet

If the received data is shorter than the minimum 14-byte header or does not
follow the packet format, the packet is rejected.

### Unknown Packet Type

Only the following packet types are valid:

- PACKET_DATA
- PACKET_ACK
- PACKET_FIN

An unknown packet type is rejected and is not processed as a DATA packet.

### Payload Too Large

A payload larger than MAX_PAYLOAD_SIZE is rejected.

MAX_PAYLOAD_SIZE = 1024

### Invalid Payload Length

The receiver verifies that the received datagram contains enough bytes for
the declared payload length.

If the complete payload is not present, the packet is rejected.

### Checksum Failure

If checksum verification fails, the packet is treated as corrupted and its
payload is not written to the output file.

### Socket Failure

If socket creation, binding, sending, or receiving fails, the program prints
an appropriate error message and performs socket cleanup before terminating.

### File Failure

If the input or output file cannot be opened, the program prints an error
message and terminates cleanly.

### Invalid Command-Line Arguments

If the required command-line arguments are missing or incorrect, the program
prints the appropriate usage message and terminates.

---

## 11. Sender Flow - Week 1

The Week 1 sender performs basic file transfer over UDP.

START
  |
  v
Check command-line arguments
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
Serialize packet
  |
  v
Send DATA packet
  |
  v
More file data?
  |                 |
 Yes                No
  |                 |
  v                 v
Read next       Create FIN packet
chunk                |
  |                  v
  +----------> Serialize FIN
                     |
                     v
                 Send FIN
                     |
                     v
                   Finish

The sender assigns a new sequence number to each DATA packet.

After all file data has been sent, the sender transmits a FIN packet to
signal the end of the transfer.

---

## 12. Receiver Flow - Week 1

The Week 1 receiver performs basic file reception over UDP.

START
  |
  v
Create UDP socket
  |
  v
Bind socket to receiver port
  |
  v
Open output file
  |
  v
Wait for UDP packet
  |
  v
Receive packet
  |
  v
Parse packet
  |
  v
Validate packet and checksum
  |
  v
Packet type?
  |                  |
 DATA               FIN
  |                  |
  v                  v
Write payload     Transfer complete
  |                  |
  v                  v
Wait for next     Close file
packet               |
                     v
                   Finish

The receiver processes DATA packets by writing their payload to the output
file.

When a valid FIN packet is received, the receiver knows that the transfer is
complete and closes the output file.

The Week 1 implementation establishes the packet format, serialization,
checksum verification, sequence numbering, and transfer termination
mechanism. Reliability features such as ACK handling, timeouts, and
retransmission are added in later stages.

---

## 13. Protocol Summary

The protocol uses UDP as the underlying transport protocol.

The application-level packet format provides:

- Packet type identification
- Sequence numbers
- Acknowledgement numbers
- Payload length
- Checksum verification
- Network byte order
- Explicit serialization
- FIN-based transfer termination

The packet format is designed so that reliability mechanisms such as
Stop-and-Wait, Go-Back-N, and Selective Repeat can be implemented later
without changing the basic wire format.

---

## 14. State Machines

Before implementing the reliability mechanisms, the sender and receiver
states are defined for the basic Week 1 file transfer.

### Sender State Machine

~~~text
IDLE
 |
 v
OPEN_FILE
 |
 v
SEND_DATA
 |
 v
CHECK_EOF
 |              |
No             Yes
 |              |
 v              v
SEND_DATA      SEND_FIN
                 |
                 v
                DONE
~~~

### Sender State Description

- **IDLE** - The sender program starts.
- **OPEN_FILE** - The input file is opened for reading.
- **SEND_DATA** - A DATA packet is created, serialized, and transmitted.
- **CHECK_EOF** - The sender checks whether more file data remains.
- **SEND_FIN** - After all DATA packets have been sent, the sender creates
  and transmits the FIN packet.
- **DONE** - The sender finishes the transfer and performs cleanup.

### Receiver State Machine

~~~text
IDLE
 |
 v
BIND_SOCKET
 |
 v
OPEN_FILE
 |
 v
WAIT_DATA
 |
 v
RECEIVE
 |
 v
CHECK_PACKET
 |              |
DATA           FIN
 |              |
 v              v
WRITE_FILE    CLOSE_FILE
 |              |
 +-----> WAIT_DATA
                |
                v
               DONE
~~~

### Receiver State Description

- **IDLE** - The receiver program starts.
- **BIND_SOCKET** - The UDP socket is created and bound to the receiver port.
- **OPEN_FILE** - The output file is opened for writing.
- **WAIT_DATA** - The receiver waits for an incoming UDP packet.
- **RECEIVE** - The receiver receives and parses the UDP datagram.
- **CHECK_PACKET** - The packet type and checksum are validated.
- **WRITE_FILE** - If the packet is a valid DATA packet, its payload is
  written to the output file.
- **CLOSE_FILE** - If the packet is a valid FIN packet, the receiver closes
  the output file.
- **DONE** - The receiver finishes the transfer and performs cleanup.

These state machines describe the basic Week 1 transfer. Later stages will
extend these states with ACK handling, timeout handling, retransmission,
duplicate detection, and protocol-specific logic for Stop-and-Wait,
Go-Back-N, and Selective Repeat.