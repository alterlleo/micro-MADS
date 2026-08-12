<div align="center">

# MICRO MADS

**A lightweight library for microcontrollers to interface with the MADS framework**

</div>

---

## Why micro-MADS?

MADS is a powerful distributed agent framework traditionally designed for full-fledged operating systems (Linux/macOS). **micro-MADS** bridges the gap between the edge and the cloud by bringing the MADS ecosystem directly to microcontrollers—starting with high-performance STM32 chips—allowing bare-metal hardware to act as first-class, lightweight network nodes without requiring an external bridge computer.

---

## How it Works

Running distributed networking protocols on a microcontroller requires a completely different mindset compared to writing software for a PC. There is no underlying operating system to manage sockets, file descriptors, or thread scheduling. 

micro-MADS solves this challenge by combining a non-blocking Finite State Machine with an event-driven network architecture.

### Embedded Networking: LwIP & Raw APIs
On a traditional OS, network operations are often *blocking* (e.g., waiting for data freezes the execution flow). On a real-time bare-metal microcontroller (like an STM32 running motor control loops or sensor acquisition), blocking the main CPU is catastrophic.

To overcome this, micro-MADS relies on **LwIP (Lightweight IP)**, an open-source TCP/IP stack designed specifically for resource-constrained embedded systems. Instead of blocking calls, micro-MADS uses LwIP's **Raw APIs** built on an **event-driven callback architecture**:
* The application registers lightweight C functions (callbacks) with the stack.
* When network events occur (such as a TCP connection establishing or data arriving from the network), LwIP asynchronously wakes up the callback to process the data, leaving the main CPU free to execute real-time tasks.

### TCP & Protocol Control Blocks (PCB)
Network connections over TCP guarantee reliable, ordered data delivery. Unlike desktop OS sockets identified by integer file descriptors, LwIP manages active connections using **Protocol Control Blocks (`tcp_pcb`)**. A `tcp_pcb` is a static memory structure holding all the state information of a specific network link (local/remote IPs, ports, sequence numbers, and callback pointers), ensuring zero dynamic memory allocation (`malloc`) and completely predictable RAM usage.

### Native MADS Protocol & Lightweight Framing
To integrate smoothly into an existing MADS deployment, micro-MADS mirrors the core negotiation sequence of the official framework, bypassing heavy dependencies like `libzmq` through direct TCP interaction:

* **The Bootstrap Handshake (REQ Pattern):** Upon booting, the microcontroller opens a TCP connection to the broker and executes the native initialization sequence[cite: 13]:
  1. It requests its specific configuration section from `mads.ini` by sending a structured multi-part frame (`[library_version, "settings", agent_name]`)[cite: 13].
  2. It synchronizes its internal clock by requesting the system timecode (`[version, "timecode"]`)[cite: 13].
* **Low-Overhead Streaming (PUB/SUB Pattern):** Once initialized, the agent processes the configuration via a zero-allocation stream parser and transitions to steady-state operation. It streams telemetry and receives commands using MADS's native legacy two-part frame format (`[topic] [json_payload]`)[cite: 13] over raw TCP, avoiding the performance overhead of heavy binary headers or MessagePack encoding on the microcontroller.