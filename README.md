<div align="center">

# MICRO MADS

**A lightweight library for microcontrollers to interface with the MADS framework**

</div>

---

## Why micro-MADS?

[MADS](https://github.com/pbosetti/MADS) is a powerful distributed agent framework traditionally designed for full-fledged operating systems (Linux/macOS). **micro-MADS** bridges the gap between the edge and the cloud by bringing the MADS ecosystem directly to microcontrollers—supporting STM32, W5500-based boards, and ESP32—allowing resource-constrained hardware to act as first-class, lightweight network nodes without requiring an external bridge computer.

---

## How it Works

Running distributed networking protocols on a microcontroller requires a completely different mindset compared to writing software for a PC. On the STM32 and W5500 routes, the application supplies the main loop and network servicing; on ESP32, the platform supplies the underlying network stack and socket implementation.

micro-MADS solves this challenge by combining a non-blocking Finite State Machine with an event-driven network architecture.

### Embedded Networking: Three Supported Architectures

#### The Software Stack (Internal MAC + LwIP):
For microcontrollers equipped with an internal Ethernet MAC, micro-MADS relies on LwIP (Lightweight IP). Instead of socket calls, it uses LwIP's Raw APIs and their event-driven callback architecture. Network events are delivered to callbacks, leaving the main loop free to perform other work. LwIP manages connection state through Protocol Control Blocks (`tcp_pcb`) allocated from its configured memory pools; configure those pools and packet buffers for the expected number of connections.

#### The Hardware Stack (External W5500 IC):
For maximum stability and to completely offload the TCP/IP processing from the microcontroller, micro-MADS supports external network controllers like the WIZnet W5500. In this architecture, the entire TCP/IP stack (IPv4, TCP, UDP, ICMP) is hardwired into the external silicon. The microcontroller does not need to run LwIP, manage complex memory pools, or handle raw packet assembly. Instead, it acts as a pure conductor, pushing and pulling payload data over a high-speed SPI bus, resulting in a dramatically smaller memory footprint and rock-solid reliability.

#### The ESP32 Stack (BSD Sockets):
On ESP32, micro-MADS uses the socket API provided by the ESP32 networking stack (the BSD-compatible `socket`, `connect`, `send`, and `recv` functions). The ESP32 therefore uses its own Wi-Fi or Ethernet network interface and does not require an external W5500 or an STM32 Ethernet MAC. The implementation uses non-blocking connection setup and polling from the application loop; call `mm_agent_spin()` regularly, just as for the other transports.

### TCP & Protocol Control Blocks (PCB)
When using the internal MAC and LwIP, network connections over TCP guarantee reliable, ordered data delivery. Unlike desktop OS sockets identified by integer file descriptors, LwIP manages active connections using **Protocol Control Blocks (`tcp_pcb`)**. A `tcp_pcb` is a static memory structure holding all the state information of a specific network link (local/remote IPs, ports, sequence numbers, and callback pointers), ensuring zero dynamic memory allocation (`malloc`) and completely predictable RAM usage.

### Native MADS Protocol & Lightweight Framing
To integrate smoothly into an existing MADS deployment, micro-MADS mirrors the core negotiation sequence of the official framework, bypassing heavy dependencies like `libzmq` through direct TCP interaction:

* **The Bootstrap Handshake (REQ Pattern):** Upon booting, the microcontroller opens a TCP connection to the broker and executes the native initialization sequence:
  1. It requests its specific configuration section from `mads.ini` (when `INI_PARSER` is enabled) by sending a structured multi-part frame whose application parts are `[library_version, "settings", agent_name]`.
  2. It synchronizes its internal clock by requesting the system timecode (application parts `[version, "timecode"]`).
* **Low-Overhead Streaming (PUB/SUB Pattern):** Once initialized, the agent processes the configuration using fixed-size buffers and transitions to steady-state operation. It streams telemetry and receives commands using MADS's native legacy two-part frame format (`[topic] [payload]`) over raw TCP. Published JSON is wrapped in the library's Snappy literal framing; no JSON or MessagePack library is required on the microcontroller.

---

## Microcontroller Requirements

To run **micro-MADS** in a resource-constrained environment, your hardware and project configuration must meet specific architectural requirements. The library is designed to keep network processing out of application-critical code, helping ensure that high-frequency real-time tasks, such as multi-axis motor control or rapid sensor acquisition, are not starved by network handling.

### Hardware Prerequisites
micro-MADS is designed to be flexible. Your hardware setup must fall into one of these three categories:

* **First Option**: Internal Ethernet (LwIP Route): An STM32 equipped with a hardware Ethernet MAC (e.g., STM32H7 series) connected to an external Physical Layer Transceiver (PHY) via RMII.
* **Second Option**: External Ethernet (W5500 Route): Any bare-metal microcontroller connected to a W5500 Ethernet module. Since the W5500 integrates both the MAC, the PHY, and the TCP/IP core, the only hardware requirement for the host MCU is a standard SPI interface and a few GPIO pins (Chip Select, Reset, and optional Interrupt).
* **Third Option**: ESP32 Socket Route: An ESP32 board with a configured Wi-Fi or Ethernet interface. No external PHY, W5500, SPI bus, or RTOS integration from micro-MADS is required; the application must only bring the ESP32 network interface up before starting the agent.

### Platform Configuration
The transport is selected at compile time. These are preprocessor definitions, so they can be supplied as compiler flags (for example, `-DUSE_ESP32`) or enabled in the project configuration. `USE_W5500` and `USE_ESP32` are mutually exclusive.

| Target | Required transport flag | Network implementation | Additional requirements |
|---|---|---|---|
| STM32 internal Ethernet | None | LwIP raw TCP API | Enable STM32 ETH and LwIP |
| MCU + W5500 | `USE_W5500` | W5500 socket API | Add the WIZnet ioLibrary headers/source and configure SPI, CS, and reset |
| ESP32 | `USE_ESP32` | ESP32 BSD socket API | Initialize and connect Wi-Fi or Ethernet before using the agent |

Do not define both transport flags. If neither is defined, the library selects the internal-MAC LwIP implementation.

The topic/configuration flags are independent of the transport:

* `INI_PARSER`: define this flag to enable the MADS INI configuration parser. The agent then obtains its publish and subscribe topics from the broker settings response.
* Without `INI_PARSER`, define `PUB_TOPIC` and `SUB_TOPIC` with the compile-time topic strings used by the application (for example, `-DPUB_TOPIC=\"telemetry\" -DSUB_TOPIC=\"commands\"`). These defaults are empty strings if the macros are omitted, so they should be set explicitly for a useful PUB/SUB connection.

#### STM32CubeMX: Internal MAC (LwIP)

* Ethernet (ETH): Under the Connectivity tab, enable the ETH peripheral and set the mode to RMII to route the pins to your external PHY.
* LwIP Middleware: Under the Middleware tab, enable LwIP. Configure network addressing and ensure you allocate sufficient RAM (Heap and Memory Pool sizes) for the network packet buffers (pbuf).

No transport flag is required for this configuration. Do not define `USE_W5500` or `USE_ESP32`.

#### External W5500 IC:

* SPI Peripheral: Under the Connectivity tab, enable a high-speed SPI peripheral in Full-Duplex Master mode.
* GPIO Configuration: Assign standard output pins for the W5500 Chip Select (CS) and Reset (RST).
* No Middleware Required: Do not enable LwIP or the internal ETH peripheral. The TCP/IP stack is entirely handled by the W5500 silicon.

Compile with `USE_W5500` and do not define `USE_ESP32`. Initialize the W5500 network settings and the ioLibrary before calling `mm_agent_spin()`. The agent uses the W5500 socket API for connection and data transfer; keep calling `mm_agent_spin()` to advance the agent state machine.

When using the W5500, TCP/IP processing is offloaded to the external controller. The host MCU prepares the MADS payload and transfers it over SPI, while the W5500 handles packet acknowledgment, retransmissions, and checksum calculations. SPI DMA can be added by the host integration when supported, but it is not required by micro-MADS itself.

#### ESP32

Define `USE_ESP32` and do not define `USE_W5500`. Include the micro-MADS sources in the ESP32/Arduino project and connect the ESP32 to the network using the platform's Wi-Fi or Ethernet API before calling `mm_agent_spin()`. The broker address passed to `mm_agent_init()` must be reachable from the active interface. No STM32CubeMX, LwIP raw callback setup, W5500 ioLibrary, or SPI configuration is needed for this route.

The ESP32 transport uses ordinary file-descriptor sockets and performs its own connection handshake. Call `mm_agent_spin()` frequently from the main loop; avoid long blocking delays, because the agent uses that call to poll socket data and advance its finite-state machine. The `USE_ESP32` flag also selects `millis()` for timekeeping and the ESP32 socket headers instead of STM32 HAL and raw LwIP headers.

For example, an Arduino-style project should define:

```text
-DUSE_ESP32 -DPUB_TOPIC=\"telemetry\" -DSUB_TOPIC=\"commands\"
```

Add `-DINI_PARSER` instead of the topic definitions when the agent should read topics from the broker configuration. The transport flag and the configuration flag are independent, so the same `INI_PARSER` choice can be used with LwIP, W5500, or ESP32.

## How to read MADS data from Microcontroller
Without a `JSON` library, a simple option for bare-metal code is to use C functions such as `strstr` and `sscanf`. Here is an example of a command callback:

```c
#include <stdio.h>
#include <string.h>

void my_command_callback(const char *topic, const char *payload) {
   // payload = "{\"motor_x\": 150.5, \"enable\": 1}"

   // check the topic
   if (strcmp(topic, "setpoint") == 0) {
      
      double target_pos = 0.0;
      int enable_flag = 0;
      
      const char *ptr_pos = strstr(payload, "\"motor_x\"");
      if (ptr_pos) {
         // extract the double after the ":" character
         sscanf(ptr_pos, "\"motor_x\": %lf", &target_pos);
      }
      
      const char *ptr_en = strstr(payload, "\"enable\"");
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
   // Let's create a buffer. Typically, 256 bytes are enough
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
