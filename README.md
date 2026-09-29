<div align="center">

# MICRO MADS

**A lightweight C library that makes a microcontroller a MADS agent**

</div>

---

## Why micro-MADS?

[MADS](https://github.com/pbosetti/MADS) is a distributed agent framework designed for full operating systems (Linux/macOS/Windows). **micro-MADS** brings a MADS agent onto a microcontroller: the board fetches its settings from the broker, publishes and receives MADS messages, and shows up in the network like any other agent, without a bridge computer.

It is written in plain C99, allocates no memory, never blocks, and needs about 7–9 KB of flash and 1.7 KB of RAM per agent.

## Supported hardware

| Target | Flag | Network |
|---|---|---|
| ESP32 (Wi-Fi or Ethernet) | `USE_ESP32` | ESP32 BSD sockets (lwIP) |
| Any MCU + WIZnet W5500 (e.g. custom STM32 boards) | `USE_W5500` | W5500 hardware TCP/IP via the WIZnet [ioLibrary_Driver](https://github.com/Wiznet/ioLibrary_Driver) |

Define exactly one of the two flags. On a desktop (Linux/macOS) the socket port is used for testing.

## How it works

The agent is a finite state machine generated with [gv2fsm](https://github.com/pbosetti/gv2fsm) from `micromads.dot` (`src/mm_fsm.c`, prefix `mm_`). Every call to `mm_agent_spin()` moves socket data and runs one step of it; no step ever waits.

```
init → disconnected → req_connecting → req_wait_settings → req_wait_timecode → pubsub_connecting → ready
                ↑                                                                                      │
                └──────────────────────────────── error ←──────────────────────────────────────────────┘
```

1. **req_connecting**: TCP connection to the broker settings port (9092) and ZMTP handshake.
2. **req_wait_settings**: sends `[version, "settings", name]`. The broker answers with the *whole* `mads.ini`; it is parsed line by line while it arrives (`mm_ini.c`), so its size does not matter. Kept: the `[agents]` ports and `timecode_fps`, and `pub_topic` / `sub_topic` of the agent's own section.
3. **req_wait_timecode**: sends `[version, "timecode"]` and stores the broker timecode; published messages carry the broker time from then on.
4. **pubsub_connecting**: connects PUB and SUB to the ports read from `mads.ini`, subscribes to `sub_topic`, publishes the `agent_event` "startup" message.
5. **ready**: publish and receive. If a connection drops, **error** closes everything and the agent starts again after 1 s, 2 s, 4 s … up to 30 s.

Each waiting state has a timeout in milliseconds (`MM_CONNECT_TIMEOUT_MS`, `MM_REPLY_TIMEOUT_MS`). The reason of the last failure is available with `mm_agent_last_error()`.

TCP delivers a byte stream, not messages: `mm_zmtp.c` reassembles ZMTP frames from whatever `recv()` returns, so split or merged packets and frames longer than 255 bytes are handled.

### Message format

Messages are exchanged in the MADS ≥ 2.4 uncompressed format, three frames: `[topic][12-byte MADS header][JSON]`.

* **Publishing**: the application builds the JSON object; micro-MADS adds `agent_id`, `hostname` and `timecode`.
* **Receiving**: the callback gets the JSON text of messages published in that format. Desktop agents use it for messages **under 256 bytes** (default `compression = "auto"`). Larger messages are snappy-compressed and MsgPack messages (`wire_format = "msgpack"`) are binary: micro-MADS drops both and counts them in `agent.dropped`. To send larger messages to a board, set in the *sender's* section of `mads.ini`:
  ```toml
  compression = "none"
  wire_format = "json"
  ```

## Usage

```c
#include "micromads.h"

static micromads_agent_t agent;

void on_message(const char *topic, const char *payload) {
  /* payload is JSON text */
}

void setup(void) {
  /* network up first (Wi-Fi, or W5500 init) */
  mm_agent_init(&agent, "espressniff", "192.168.1.10", 9092); /* name = section in mads.ini */
  mm_agent_set_callback(&agent, on_message);
}

void loop(void) {
  mm_agent_spin(&agent);                       /* call it on every loop */
  if (mm_agent_is_ready(&agent))
    mm_agent_publish(&agent, "{\"temp\": 21.5}"); /* on pub_topic from mads.ini */
}
```

The agent's section in `mads.ini` (served by the broker):

```toml
[espressniff]
pub_topic = "sensors"
sub_topic = ["command", "setpoint"]   # or a single string
```

If the section has no `pub_topic`, the agent name is used (as desktop agents do). `PUB_TOPIC` / `SUB_TOPIC` compiler definitions override the file.

The state constants (`MM_STATE_READY`, …) are in `mm_fsm.h`; `mm_agent_state_name()` returns the state as text.

### ESP32 (PlatformIO, Arduino framework)

```ini
build_flags = -D USE_ESP32
lib_deps = symlink:///path/to/micro-MADS
```

Connect Wi-Fi before starting the agent. See `examples/esp32/main.cpp`. Avoid long `delay()`s in `loop()`: data waits in the socket buffers, but timeouts and message latency are measured from `mm_agent_spin()`.

### W5500 (STM32 HAL or bare metal)

1. Add the WIZnet ioLibrary_Driver (`Ethernet/socket.c`, `Ethernet/wizchip_conf.c`, `Ethernet/W5500/w5500.c`) and define `_WIZCHIP_=W5500`.
2. Configure SPI, CS and reset, register the SPI callbacks (`reg_wizchip_cs_cbfunc`, `reg_wizchip_spi_cbfunc`) and set IP/MAC (`wizchip_init`, `wizchip_setnetinfo`) before starting the agent.
3. Compile micro-MADS with `-DUSE_W5500`. Time comes from `HAL_GetTick()`; on other platforms define `MM_GET_TICK()`.
4. Hardware sockets 0, 1 and 2 are used (settings, PUB, SUB); change them with `MM_W5500_SOCK_REQ/PUB/SUB`.

## Configuration

All options are in `inc/mm_config.h` and can be set as compiler flags:

| Option | Default | Meaning |
|---|---|---|
| `MM_MADS_VERSION` | `"v2.4.3"` | must match the broker's `vMAJOR.MINOR` |
| `MM_RX_BUF_SIZE` | 512 | largest received JSON |
| `MM_TX_BUF_SIZE` | 512 | largest published message |
| `MM_MAX_TOPIC_LEN` / `MM_MAX_SUB_TOPICS` | 32 / 4 | topic length / number of sub topics |
| `MM_INI_LINE_LEN` | 128 | longer `mads.ini` lines are skipped |
| `MM_CONNECT_TIMEOUT_MS` / `MM_REPLY_TIMEOUT_MS` | 3000 | timeouts |
| `MM_RETRY_MIN_MS` / `MM_RETRY_MAX_MS` | 1000 / 30000 | reconnection delay |

## Reading and writing JSON

Without a JSON library, `strstr` and `sscanf` are enough for simple commands:

```c
void on_message(const char *topic, const char *payload) {
  if (strcmp(topic, "setpoint") == 0) {
    double x = 0.0;
    const char *p = strstr(payload, "\"motor_x\"");
    if (p) sscanf(p, "\"motor_x\": %lf", &x);
  }
}
```

and `snprintf` to publish:

```c
char json[128];
snprintf(json, sizeof(json), "{\"data_a\": %d, \"status\": \"running\"}", data_a);
mm_agent_publish(&agent, json);
```

On STM32 with newlib-nano, `%f` in `printf`/`scanf` requires enabling float support (`-u _printf_float`, `-u _scanf_float`); micro-MADS itself uses integer formatting only.

## Testing on a PC

With a MADS broker running:

```sh
cd test
make                                     # unit tests: mads.ini parser, ZMTP frames
make live BROKER=127.0.0.1 NAME=stm32_board   # the agent against the broker
```

`make live` prints the state changes, the settings read from `mads.ini`, publishes a message every 500 ms and prints what it receives; `mads feedback` shows the published messages.
