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

* **The Bootstrap Handshake (REQ Pattern):** Upon booting, the microcontroller opens a TCP connection to the broker and executes the native initialization sequence:
  1. It requests its specific configuration section from `mads.ini` by sending a structured multi-part frame (`[library_version, "settings", agent_name]`).
  2. It synchronizes its internal clock by requesting the system timecode (`[version, "timecode"]`).
* **Low-Overhead Streaming (PUB/SUB Pattern):** Once initialized, the agent processes the configuration via a zero-allocation stream parser and transitions to steady-state operation. It streams telemetry and receives commands using MADS's native legacy two-part frame format (`[topic] [json_payload]`) over raw TCP, avoiding the performance overhead of heavy binary headers or MessagePack encoding on the microcontroller.

---

## Microcontroller Requirements

To run **micro-MADS** on a bare-metal environment, your hardware and project configuration must meet specific architectural requirements. The library is purposely designed to offload network operations from the main CPU, ensuring that your critical, high-frequency real-time tasks—such as multi-axis motor control or rapid sensor acquisition—are never starved or interrupted.

### Hardware Prerequisites
* **Microcontroller with Hardware MAC:** An STM32 equipped with a hardware Ethernet MAC (e.g., the STM32H7 series, commonly found on Nucleo-H7 boards).
* **Physical Layer Transceiver (PHY):** An external Ethernet PHY chip connected to the microcontroller to translate MAC data into electrical signals over the RJ45 cable.

### STM32CubeMX Configuration
micro-MADS does not require an RTOS. However, the underlying network infrastructure must be configured via STM32CubeMX before including the library:

1. **Ethernet (ETH) Peripheral:** 
  
   * Located under the *Connectivity* tab, enable the ETH peripheral.
   * Set the mode to **RMII** (Reduced Media Independent Interface) to properly route the physical pins of your STM32 to the external PHY chip.
2. **LwIP (Lightweight IP) Middleware:**
   
   * Located under the *Middleware* tab, enable **LwIP**. 
   * This action imports the necessary TCP/IP stack into your project. Within the LwIP parameters, you can configure network addressing (DHCP or Static IP).
   * *Note:* Ensure you allocate sufficient RAM (Heap and Memory Pool sizes) for the network packet buffers (`pbuf`) based on your expected MADS payload sizes.

### Hardware Networking
micro-MADS achieves its zero-blocking performance by heavily leveraging the STM32 hardware architecture rather than raw CPU cycles:

* **RAM-Only Operations:** When the agent publishes a telemetry message, micro-MADS simply copies your payload into an LwIP packet buffer (`pbuf`) in RAM. LwIP then rapidly appends the necessary TCP, IP, and MAC headers.
* **DMA (Direct Memory Access) Offloading:** The CPU does not wait for the data to physically travel over the wire. Instead, the low-level STM32 Ethernet driver instructs the DMA controller to fetch the assembled packet directly from RAM and push it to the hardware MAC.
* **Background Transmission:** The MAC automatically forwards the byte stream to the PHY, and finally out to the Ethernet cable.

Because the entire transmission pipeline relies on DMA and hardware peripherals, the network function calls return in a fraction of a microsecond. Your CPU is immediately freed to resume executing its precise state machine and real-time control loops, while the network traffic flows completely in the background.

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