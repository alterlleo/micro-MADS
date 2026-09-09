<div align="center">

# MICRO MADS

**A lightweight library for microcontrollers to interface with the MADS framework**

</div>

---

## Why micro-MADS?

[MADS](https://github.com/pbosetti/MADS) is a powerful distributed agent framework traditionally designed for full-fledged operating systems (Linux/macOS). **micro-MADS** bridges the gap between the edge and the cloud by bringing the MADS ecosystem directly to microcontrollers—starting with high-performance STM32 chips—allowing bare-metal hardware to act as first-class, lightweight network nodes without requiring an external bridge computer.

---

## How it Works

Running distributed networking protocols on a microcontroller requires a completely different mindset compared to writing software for a PC. There is no underlying operating system to manage sockets, file descriptors, or thread scheduling. 

micro-MADS solves this challenge by combining a non-blocking Finite State Machine with an event-driven network architecture.

### Embedded Networking: Dual Architecture Support

#### The Software Stack (Internal MAC + LwIP):
For microcontrollers equipped with an internal Ethernet MAC, micro-MADS relies on LwIP (Lightweight IP). Instead of blocking calls, it uses LwIP's Raw APIs built on an event-driven callback architecture. When network events occur, LwIP asynchronously wakes up the callback to process the data, leaving the main CPU free. It manages connections using Protocol Control Blocks (tcp_pcb) to ensure zero dynamic memory allocation.

#### The Hardware Stack (External W5500 IC):
For maximum stability and to completely offload the TCP/IP processing from the microcontroller, micro-MADS supports external network controllers like the WIZnet W5500. In this architecture, the entire TCP/IP stack (IPv4, TCP, UDP, ICMP) is hardwired into the external silicon. The microcontroller does not need to run LwIP, manage complex memory pools, or handle raw packet assembly. Instead, it acts as a pure conductor, pushing and pulling payload data over a high-speed SPI bus, resulting in a dramatically smaller memory footprint and rock-solid reliability.

### TCP & Protocol Control Blocks (PCB)
When using the internal MAC and LwIP, network connections over TCP guarantee reliable, ordered data delivery. Unlike desktop OS sockets identified by integer file descriptors, LwIP manages active connections using **Protocol Control Blocks (`tcp_pcb`)**. A `tcp_pcb` is a static memory structure holding all the state information of a specific network link (local/remote IPs, ports, sequence numbers, and callback pointers), ensuring zero dynamic memory allocation (`malloc`) and completely predictable RAM usage.

### Native MADS Protocol & Lightweight Framing
To integrate smoothly into an existing MADS deployment, micro-MADS mirrors the core negotiation sequence of the official framework, bypassing heavy dependencies like `libzmq` through direct TCP interaction:

* **The Bootstrap Handshake (REQ Pattern):** Upon booting, the microcontroller opens a TCP connection to the broker and executes the native initialization sequence:
  1. It requests its specific configuration section from `mads.ini` by sending a structured multi-part frame (`[library_version, "settings", agent_name]`).
  2. It synchronizes its internal clock by requesting the system timecode (`[version, "timecode"]`).
* **Low-Overhead Streaming (PUB/SUB Pattern):** Once initialized, the agent processes the configuration via a zero-allocation stream parser and transitions to steady-state operation. It streams telemetry and receives commands using MADS's native legacy two-part frame format (`[topic] [json_payload]`) over raw TCP, avoiding the performance overhead of heavy binary headers or MessagePack encoding on the microcontroller.

---

## Microcontroller Requirements

To run **micro-MADS** on a bare-metal environment, your hardware and project configuration must meet specific architectural requirements. The library is purposely designed to offload network operations from the main CPU, ensuring that your critical, high-frequency real-time tasks—such as multi-axis motor control or rapid sensor acquisition—are never starved or interrupted.

### Hardware Prerequisites
micro-MADS is designed to be flexible. Your hardware setup must fall into one of these two categories:

* **First Option**: Internal Ethernet (LwIP Route): An STM32 equipped with a hardware Ethernet MAC (e.g., STM32H7 series) connected to an external Physical Layer Transceiver (PHY) via RMII.
* **Second Option**: External Ethernet (W5500 Route): Any bare-metal microcontroller connected to a W5500 Ethernet module. Since the W5500 integrates both the MAC, the PHY, and the TCP/IP core, the only hardware requirement for the host MCU is a standard SPI interface and a few GPIO pins (Chip Select, Reset, and optional Interrupt).

### STM32CubeMX Configuration
Depending on your hardware choice, the setup process in STM32CubeMX differs completely. No RTOS is required for either method.

#### For Internal MAC (LwIP):

* Ethernet (ETH): Under the Connectivity tab, enable the ETH peripheral and set the mode to RMII to route the pins to your external PHY.
* LwIP Middleware: Under the Middleware tab, enable LwIP. Configure network addressing and ensure you allocate sufficient RAM (Heap and Memory Pool sizes) for the network packet buffers (pbuf).

#### For External W5500 IC:

* SPI Peripheral: Under the Connectivity tab, enable a high-speed SPI peripheral in Full-Duplex Master mode.
* GPIO Configuration: Assign standard output pins for the W5500 Chip Select (CS) and Reset (RST).
* No Middleware Required: Do not enable LwIP or the internal ETH peripheral. The TCP/IP stack is entirely handled by the W5500 silicon.

When using the W5500, the network offloading reaches its maximum potential. The host MCU simply prepares the MADS payload and triggers an SPI transfer. By pairing the SPI peripheral with the STM32's DMA controller, the payload is clocked out to the W5500 entirely in the background. The CPU returns to its control loops instantly, while the external IC handles packet acknowledgment, retransmissions, and checksum calculations in hardware.

## How to read MADS data from Microcontroller
Without any `JSON` library, the best choice for the bare-metal coding is to use the C functions as `strstr` and `sscanf`. Here is an example of the function `my_callback`:

```c
#include <stdio.h>
#include <string.h>

void my_command_callback(const char *topic, const char *payload) {
   // payload = "{\"motor_x\": 150.5, \"enable\": 1}"

   // check the topic
   if (strcmp(topic, "setpoint") == 0) {
      
      double target_pos = 0.0;
      int enable_flag = 0;
      
      char *ptr_pos = strstr(payload, "\"motor_x\"");
      if (ptr_pos) {
         // extract the double after the ":" character
         sscanf(ptr_pos, "\"motor_x\": %lf", &target_pos);
      }
      
      char *ptr_en = strstr(payload, "\"enable\"");
      if (ptr_en) {
         sscanf(ptr_en, "\"enable\": %d", &enable_flag);
      }
      
      // Use the data
      // ---
   }
}

```

### How to publish data into MADS network
As the whole library is operating without `JSON` libraries, it is mandatory to create the message according to `JSON` layout, paying attention to add the `\"` escape:

```c
void publish_telemetry(int data_a, int data_b, double data_c) {
   // Let's create a buffer. Tipically, 256 bytes are enough
   char json_buffer[256];
   // format the JSON text
   snprintf(json_buffer, sizeof(json_buffer), 
            "{"
            "\"data_a\": %d, "
            "\"data_b\": %d, "
            "\"data_c\": %.2f, "
            "\"status\": \"running\""
            "}", 
            data_a, data_b, data_c);

   // The resulting JSON message will be displayed like this:
   // {"data_a": 1200, "data_b": -450, "data_c": 35.50, "status": "running"}
   
   // publish
   if (mm_agent_publish(&my_agent, json_buffer)) {
      // data published
   } else {
      // error, agent not ready or disconnected
   }
}
