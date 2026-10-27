# Reliable Data Transfer over UDP

A C-based implementation of reliable file transfer over UDP using **Stop-and-Wait, Go-Back-N, and Selective Repeat ARQ**, with network impairment simulation and experimental performance evaluation.

## About the Project

UDP is a connectionless transport protocol that does not guarantee packet delivery, ordering, or duplicate protection. This project implements reliability mechanisms on top of UDP to support accurate file transfer under different network conditions.

The system includes three ARQ protocols and a configurable channel emulator that simulates packet loss, corruption, duplication, reordering, and delay with jitter.

The project evaluates the behaviour of the protocols through automated experiments, file-transfer checks, and recorded CSV results.

## Objectives

- Implement and evaluate three ARQ protocols over UDP.
- Transfer files reliably between a sender and receiver.
- Detect packet corruption using checksums.
- Handle packet loss and missing acknowledgements through retransmissions.
- Manage packet sequence numbers and acknowledgements.
- Simulate unreliable network conditions.
- Verify transfer and file integrity.
- Compare the observed behaviour of the three protocols under different conditions.

## Protocols Implemented

### 1. Stop-and-Wait ARQ

The sender transmits one packet and waits for its acknowledgement before sending the next packet. If an acknowledgement is not received within the timeout, the sender can retransmit the packet.

**Key characteristic:** Simple implementation with at most one outstanding data packet.

### 2. Go-Back-N ARQ

The sender can transmit multiple packets within a sliding window without waiting for each individual acknowledgement. When a packet cannot be accepted or acknowledged successfully, the sender may retransmit that packet and subsequent outstanding packets.

**Key characteristic:** Supports pipelining, but retransmissions may include packets that were already received successfully.

### 3. Selective Repeat ARQ

The sender can transmit multiple packets within a sending window. The receiver can buffer out-of-order packets, and the sender can retransmit individual packets that require recovery.

**Key characteristic:** Can reduce unnecessary retransmissions, but requires more buffering and state management.

## Key Features

- UDP-based file transfer.
- Stop-and-Wait, Go-Back-N, and Selective Repeat implementations.
- Packet sequence numbers and acknowledgements.
- Packet checksum calculation and verification.
- Retransmission and timeout handling.
- Adaptive retransmission timeout support in the project.
- Configurable network impairment simulation.
- Automated experiment scripts.
- CSV-based recording of experiment results.
- Transfer-success and integrity-status checks.

## Network Impairment Scenarios

The experiment suites cover six scenarios:

| Scenario | Purpose |
|---|---|
| Baseline | Establish a reference under normal simulated conditions. |
| 10% Packet Loss | Evaluate recovery from dropped packets. |
| 10% Packet Corruption | Evaluate detection and recovery from corrupted packets. |
| Packet Duplication | Test handling of duplicate packet delivery. |
| Packet Reordering | Test handling of packets arriving out of order. |
| Delay and Jitter | Evaluate behaviour under simulated delay and variable packet timing. |

These scenarios help evaluate the reliability mechanisms and recovery behaviour of each protocol.

## Technologies Used

- **Programming language:** C
- **Transport protocol:** UDP
- **Compiler:** GCC
- **Development environment:** Windows with MSYS2 UCRT64
- **Networking library:** Winsock (`ws2_32`) for the Windows build
- **Version control:** Git and GitHub
- **Experiment output:** CSV files
- **Testing:** Automated experiment scripts and file-transfer integrity checks

## Project Structure

```text
reliable-data-transfer-udp/
├── data/           # Test data
├── docs/           # Project documentation and reports
├── experiments/    # Experiment scripts' outputs and CSV results
├── include/        # Header files
├── scripts/        # Automated experiment scripts
├── src/            # Sender, receiver, and protocol source code
├── tests/          # Testing utilities
├── .gitignore
├── AI-USE.md       # AI usage disclosure
├── Makefile
└── README.md
```

The folders may contain additional files as development and testing progress.

## Requirements

To build and run the project using the tested Windows setup, install:

- Windows
- MSYS2 with the UCRT64 environment
- GCC
- Bash
- Git (for cloning and version control)

The Windows build links against Winsock using `-lws2_32`.

## Build Instructions

Open the MSYS2 UCRT64 terminal and navigate to the project directory. For example:

```bash
cd /c/Users/KIIT/reliable-data-transfer-udp
```

Other team members should use the path where they cloned the repository.

### Compile the Receiver

```bash
gcc -Wall -Wextra -Iinclude -o receiver.exe src/receiver.c src/packet.c src/checksum.c -lws2_32
```

### Compile the Go-Back-N Experiment Sender

```bash
gcc -Wall -Wextra -Iinclude -o sender_gbn_experiment.exe src/sender_gbn_experiment.c src/packet.c src/checksum.c -lws2_32
```

These commands were verified in the project's Windows/MSYS2 environment. They compile the receiver and the Go-Back-N experiment sender; they are not intended to replace the experiment scripts' own build steps.

## Running Experiments

Run the commands below from the project root in the MSYS2 UCRT64 terminal.

### Selective Repeat

```bash
bash scripts/run_experiments.sh
```

### Go-Back-N

```bash
bash scripts/run_gbn_experiments.sh
```

### Stop-and-Wait

```bash
bash scripts/run_stop_wait_experiments.sh
```

Allow each script to finish before starting the next one. The scripts run the configured experiment scenarios and record results in CSV files under `experiments/`.

**Note:** Running the scripts may update their corresponding result files. Preserve a copy of previously recorded results if you need to retain a specific experiment run.

## Experiment Results

The latest recorded CSV files are:

- `experiments/results.csv` — Selective Repeat results.
- `experiments/gbn_results.csv` — Go-Back-N results.
- `experiments/stop_wait_results.csv` — Stop-and-Wait results.

The latest supplied results contain six scenarios per protocol, for a total of 18 protocol/scenario runs. All 18 rows report:

- `transfer_ok = 1`
- `integrity_ok = 1`

These values indicate that the recorded runs passed their transfer and integrity checks.

### Observed Results

The recorded elapsed times in seconds are shown below.

| Scenario | Selective Repeat | Go-Back-N | Stop-and-Wait |
|---|---:|---:|---:|
| Baseline | 0 | 0 | 1 |
| 10% Packet Loss | 2 | 5 | 8 |
| 10% Packet Corruption | 1 | 2 | 5 |
| Duplication | 0 | 1 | 0 |
| Reordering | 1 | 1 | 2 |
| Delay and Jitter | 1 | 10 | 4 |

*Source: the latest supplied experiment CSV files, recorded on 27 October 2026.*

These are recorded elapsed-time values, not precise network-latency measurements. In particular, a recorded value of zero may mean the run completed in less than one second or that the timing was rounded down.

The results describe the observed runs only. A stronger performance comparison would require repeated trials, documented input-file sizes, higher-resolution timing, and additional metrics such as goodput and retransmission counts.

## Team Members and Responsibilities

The following responsibilities reflect the team's stated project allocation.

- **Sibanandan Dash:** Common packet format, UDP socket layer, Stop-and-Wait ARQ, checksum, and integrity verification.
- **Jagannath Parida:** Go-Back-N ARQ, sliding-window mechanism, cumulative acknowledgements, and retransmission logic.
- **Swastik Swain:** Selective Repeat ARQ, receiver buffering, selective acknowledgements, and retransmission management.
- **Sibasis Mohanty:** Channel emulator, experiment framework, data collection, plotting, and performance analysis.

## Limitations

- Recorded elapsed times are coarse and should not be treated as precise latency measurements.
- The latest result files do not provide repeated-trial statistics.
- Goodput, throughput, and retransmission-count comparisons are not included in the supplied CSV results.
- Results are limited to the implemented protocols and configured test scenarios.
- Performance may differ with different file sizes, window sizes, network conditions, and system environments.

## Future Improvements

- Repeat experiments and report mean and median measurements.
- Test multiple input-file sizes and window configurations.
- Record retransmission counts and useful-data goodput.
- Improve performance visualisation and comparative analysis.
- Expand tests to cover additional network conditions.
- Improve experiment reproducibility and reporting.

## AI Usage

AI tools were used as supplementary assistance for explanations, troubleshooting, and documentation, where applicable. The project team is responsible for reviewing the implementation, validating experimental results, and ensuring the final submission is accurate. Further details are provided in `AI-USE.md`.

## License and Course Information

This project was developed as part of **CS-30003 Coding Assignment 1**.

No open-source license is specified in this README. If the course requires a particular license, add the appropriate license file and reference it here.
