# Reliable Data Transfer over UDP

## About the Project
This project implements a reliable file transfer system over UDP using three Automatic Repeat reQuest (ARQ) protocols: Stop-and-Wait, Go-Back-N, and Selective Repeat.

UDP does not guarantee packet delivery or order. Our system adds reliability using sequence numbers, checksums, acknowledgements, retransmission timers, and adaptive timeout estimation. A channel emulator simulates network conditions such as packet loss, duplication, reordering, corruption, and delay.

## Objectives
- Implement three ARQ protocols over UDP.
- Ensure reliable and accurate file transfer.
- Handle packet loss and network errors through retransmissions.
- Verify file integrity using cryptographic hashes.
- Compare protocol performance under different network conditions.

## Protocols Implemented

### 1. Stop-and-Wait
The sender transmits one packet and waits for an acknowledgement before sending the next packet.

### 2. Go-Back-N
The sender transmits multiple packets within a sliding window. If a packet is lost, the sender retransmits that packet and subsequent packets in the window.

### 3. Selective Repeat
The sender retransmits only the packets that are lost or require retransmission. The receiver buffers out-of-order packets.

## Technologies Used
- **Language:** C
- **Networking:** POSIX UDP sockets
- **Concurrency:** Pthreads and operating system primitives, where required
- **Integrity:** Cryptographic hashing and checksums
- **Experiment Analysis:** Python, Matplotlib, Pandas, and NumPy
- **Version Control:** Git and GitHub

## Key Features
- UDP-based file transfer
- Three interchangeable ARQ protocols
- Sequence numbers and acknowledgements
- Packet checksums and retransmission timers
- Adaptive Retransmission Timeout (RTO)
- Simulated packet loss, duplication, reordering, corruption, and delay with jitter
- Source and received file integrity verification
- Performance measurement and comparison

## Performance Evaluation
The project evaluates the three protocols under controlled network conditions.

The main metrics are:
- **Goodput:** Useful data delivered per unit of time
- **Retransmission Count:** Number of packets retransmitted
- **Transfer Time:** Time required to transfer a file
- **Window Size:** Effect of the sender's window size on performance

Results will be presented using comparative plots and analysis.

## Project Structure
The exact structure depends on the implementation. A possible structure is:

```text
reliable-data-transfer-udp/
├── src/
│   ├── sender.c
│   ├── receiver.c
│   ├── stop_and_wait.c
│   ├── go_back_n.c
│   └── selective_repeat.c
├── tests/
├── experiments/
├── plots/
├── README.md
└── AI-USE.md
```

*Note: Update this structure to match the actual files in your repository.*

## How to Build and Run
The exact commands depend on the current implementation.

For a C-based project using GCC, a basic compilation command may look like:

```bash
gcc -Wall -Wextra -pthread src/sender.c -o sender
```

Compile the receiver separately if it has its own source file. Run the sender and receiver according to the implemented command-line arguments.

Refer to the source files and update this section with the verified build and execution commands.

## Team Members
- **Sibanandan Dash:** Common packet format, UDP socket layer, Stop-and-Wait ARQ, checksum, and integrity verification
- **Jagannath Parida:** Go-Back-N ARQ, sliding-window mechanism, cumulative acknowledgements, and retransmission logic
- **Swastik Swain:** Selective Repeat ARQ, receiver buffering, selective acknowledgements, and retransmission management
- **Sibasis Mohanty:** Channel emulator, experiment framework, data collection, plotting, and performance analysis

## Future Improvements
- Improve protocol performance under high packet loss
- Test additional network conditions
- Optimize retransmission timeout estimation
- Extend experimental evaluation and analysis

## License
This project was developed as part of the CS-30003 Coding Assignment 1.
