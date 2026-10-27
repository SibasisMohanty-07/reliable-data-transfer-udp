# AI Usage Disclosure

## 1. Overview

We used AI tools throughout the development of our Reliable Data Transfer over UDP project to understand networking concepts, plan the implementation, solve problems, test our code, analyze experimental results, and prepare project documentation.

AI acted as a learning and development assistant. We used its suggestions to understand different approaches and improve our implementation, while the project was developed and reviewed in the context of our team's work.

## 2. Areas Where AI Was Used

### Project Planning and Design
- Understanding the project requirements and breaking them into smaller tasks.
- Discussing the design of Stop-and-Wait, Go-Back-N, and Selective Repeat ARQ protocols.
- Understanding packet formats, sequence numbers, acknowledgements, checksums, and retransmission timers.
- Planning the channel emulator and performance experiments.

### Learning and Concept Explanation
- Understanding UDP sockets and reliable data transfer mechanisms.
- Learning about sliding windows, cumulative and selective acknowledgements, RTT estimation, adaptive RTO, and Karn's algorithm.
- Comparing the behaviour of the three ARQ protocols under different network conditions.

### Code Development and Debugging
- Getting guidance on implementation structure, C programming, socket communication, and error handling.
- Understanding compiler errors, runtime issues, and unexpected protocol behaviour.
- Reviewing code logic and exploring possible fixes for bugs.
- Improving code readability, modularity, and maintainability.

AI-generated suggestions were treated as assistance and required review and testing before being used. The team remained responsible for understanding the code and ensuring that the implementation followed the assignment requirements.

### Testing and Experiments
- Developing ideas for correctness tests and edge cases.
- Planning tests involving packet loss, duplication, reordering, corruption, and delay.
- Understanding how to verify transferred-file integrity using hashes.
- Getting guidance on collecting performance data and measuring goodput, transfer time, and retransmission counts.

### Performance Analysis and Documentation
- Getting assistance with experiment scripts, data analysis, and plotting.
- Understanding and presenting comparisons between Stop-and-Wait, Go-Back-N, and Selective Repeat.
- Preparing and improving the README, performance report, and other project documentation.

## 3. Tools Used

- **AI assistants:** For explanations, planning, coding guidance, debugging, testing suggestions, and documentation.
- **Git and GitHub:** For version control and collaboration.
- **C compiler and testing tools:** For building and testing the implementation.
- **Python plotting and analysis libraries:** For experimental data analysis and visualization, where applicable.

## 4. Verification and Responsibility

AI suggestions may contain errors or incomplete explanations. Therefore, suggestions needed to be checked against the project requirements and evaluated through code review, compilation, and testing where applicable.

The team is responsible for the correctness, reliability, and final submission of the project. Every team member is expected to understand the components they worked on and be able to explain the design decisions, implementation, and experimental results during the viva.

## 5. Academic Integrity

We acknowledge the use of AI assistance throughout the project and disclose it here in accordance with the course guidelines.

AI assistance does not replace the team's responsibility to understand and verify the system. The final submission must comply with the assignment's restriction against submitting AI-generated core protocol implementations as our own work.

This disclosure is intended to provide a transparent record of how AI tools supported our learning and development process.
