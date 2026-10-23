# Architecture and Specification of asyncTelegramAPI

📖 **[Версия на русском языке](ARCHITECTURE_RU.md)**

## 1. Introduction and Project Purpose

**asyncTelegramAPI** is a lightweight, fully non-blocking Telegram Bot API library for **ESP8266** and **ESP32** microcontrollers operating via **Wi-Fi**.

The library is designed with **zero external network dependencies**. It uses standard system TLS clients (`BearSSL::WiFiClientSecure` on ESP8266 and `WiFiClientSecure` on ESP32) driven by an efficient non-blocking Finite State Machine (FSM) featuring HTTP Keep-Alive and optional background FreeRTOS task mode on ESP32.

### Key Advantages:
- **Zero External Network Dependencies:** No third-party async network libraries (`AsyncTCP`, `ESPAsyncTCP`). Only rock-solid system sockets and `ArduinoJson ^7.0.0`.
- **Zero-blocking FSM:** The state machine step `bot.tick()` in `loop()` executes in less than 1 microsecond when no incoming data is present, keeping the CPU 100% free for sensors, buttons, and displays.
- **ESP32 FreeRTOS Task Mode:** Optional background worker on Core 1 (`AsyncTelegramAPI bot(true);`), completely freeing the user from calling `bot.tick()` in `loop()`.
- **Tx FIFO Outbox Queue:** Outbox queue holding up to 32 outgoing messages with absolute transmission priority over polling.
- **Smart Interactive Window:** Infrequent short-polling in idle mode (closing socket to release RAM) and automatic fast Long Polling acceleration upon incoming user commands.
- **Auto HTML & Menu:** Automatic parsing mode detection (`</`) and keyboard layout using `[MENU]`.
- **Supergroup Topics & Edit Message:** Full support for message threads (`threadId`) and in-place message editing with delivery confirmation callbacks.
- **RAM & Flash Optimization:** String literals placed strictly into PROGMEM (`F()`, `PSTR()`) and full removal of all logging code and strings when `#define DEBUG` is disabled.

---

## 2. Architecture Evolution and Rejection of External AsyncTCP Libraries

During the initial design phase, external asynchronous TCP libraries (`AsyncTCP` for ESP32 and `ESPAsyncTCP` for ESP8266) were considered. However, practical analysis and stress-testing revealed several fundamental issues:

1. **TLS/SSL Instability in AsyncTCP:**
   Telegram Bot API strictly operates over HTTPS (port 443). In third-party `AsyncTCP` and `ESPAsyncTCP` libraries, TLS support is implemented via community wrappers or patches. These wrappers suffer from memory leaks upon reconnects, heap fragmentation, and intermittent Kernel Panics / Watchdog Resets.
2. **Framework and Core Version Conflicts:**
   `ESPAsyncTCP` and `AsyncTCP` frequently break when upgrading Arduino core versions (ESP-IDF 5.x and ESP8266 core 3.x), requiring custom forks and maintenance burden.
3. **Socket Exhaustion in lwIP:**
   Opening and closing async TCP handles rapidly causes sockets to linger in `TIME_WAIT` state (up to 60 seconds), exhausting the limited pool of lwIP socket descriptors.

### Chosen Solution: Non-blocking FSM + Keep-Alive + FreeRTOS
Instead of relying on unstable external network libraries, the architecture was built on:
- System-level **`BearSSL::WiFiClientSecure`** (on ESP8266 with bounded buffers 1024/512 to preserve RAM).
- System-level **`WiFiClientSecure`** (on ESP32).
- A non-blocking Finite State Machine (FSM) inspecting `client.available()`.
- Persistent **HTTP Keep-Alive** connection, eliminating repeated TLS handshake overhead.
- On ESP32 — an optional FreeRTOS worker (`xTaskCreatePinnedToCore`) isolating network activity from application logic.

---

## 3. Layered Architecture

```
+-------------------------------------------------------------+
|                        User Sketch                          |
|         setup(), loop() [100% free], callback()             |
+-------------------------------------------------------------+
                              |
+-------------------------------------------------------------+
|                      AsyncTelegramAPI                       |
|   - start() / stop() / tick() / checkMessage()              |
|   - sendMessage(), editMessage(), deleteMessage()           |
|   - Tx FIFO Queue (Priority outbox queue)                   |
|   - Interactive Window & Polling Timer                      |
|   - FreeRTOS Task Worker (ESP32)                            |
+-------------------------------------------------------------+
                              |
+-------------------------------------------------------------+
|                        TelegramCore                         |
|   - TResult structure (with threadId topic support)         |
|   - JSON generation for getUpdates, sendMessage...          |
|   - Telegram API JSON parser (ArduinoJson 7)                |
|   - [MENU] keyboard parser, whitelist, is_command           |
+-------------------------------------------------------------+
                              |
+-------------------------------------------------------------+
|                     System Network Layer                    |
|     ESP32: WiFiClientSecure   |   ESP8266: BearSSL          |
|                 (TLS 1.2 / native lwIP)                     |
+-------------------------------------------------------------+
```

---

## 4. Finite State Machine (FSM) States

The `AsyncTelegramAPI` engine operates without any blocking `delay()` calls, transitioning states as socket data arrives:

```
                  +-------------------+
                  |      TG_IDLE      |<-----------------------+
                  +-------------------+                        |
                     |  |                                      |
       (Tx item in   |  | (readyToPoll timer                   |
        queue)       |  |  expired)                            |
                     v  v                                      |
         +----------------------+                              |
         |   _ensureConnected   |                              |
         | (TLS handshake 443)  |                              |
         +----------------------+                              |
                     |                                         |
                     v                                         |
         +----------------------+                              |
         |   _sendHttpRequest   |                              |
         | (POST /bot... json)  |                              |
         +----------------------+                              |
                     |                                         |
                     v                                         |
         +----------------------+                              |
         |     TG_WAIT_DATA     | (Urgent Tx aborts poll!)     |
         +----------------------+                              |
                     | _client.available() > 0                 |
                     v                                         |
         +----------------------+                              |
         |   TG_READ_HEADERS    |                              |
         | (line-by-line parse) |                              |
         +----------------------+                              |
                     | empty line \r\n found                  |
                     v                                         |
         +----------------------+                              |
         |     TG_READ_BODY     |                              |
         | (read Content-Length)|                              |
         +----------------------+                              |
                     | full body received                      |
                     v                                         |
         +----------------------+                              |
         |  _parseResponseBody  |                              |
         | (invoke user cb)     |                              |
         +----------------------+                              |
                     |                                         |
                     +-----------------------------------------+
```

1. **TG_IDLE:** Idle state. Evaluates the outbox queue (`_txQueue`) and the polling interval timer (`readyToPoll`).
2. **_ensureConnected:** Connection check. If the socket is closed, initiates a secure TLS handshake to `api.telegram.org:443`.
3. **_sendHttpRequest:** Non-blocking transmission of the HTTP POST request (API endpoint, tokens, Keep-Alive/Close headers, serialized JSON payload).
4. **TG_WAIT_DATA:** Passive wait for server response:
   - While server holds Long Polling, the engine waits without consuming CPU cycles.
   - **Urgent Tx Preemption:** If the application queues an outgoing message while Long Polling is active, the socket is immediately terminated to send the Tx message without delay!
5. **TG_READ_HEADERS:** Line-by-line header reading. Extracts HTTP status code (`HTTP/1.1 200 OK`) and `Content-Length`.
6. **TG_READ_BODY:** Stream reading of the HTTP response body until `Content-Length` is satisfied.
7. **_parseResponseBody:** JSON deserialization via ArduinoJson 7:
   - If response was for a Tx message — triggers callback `onSent(messageId)`.
   - If response was for `getUpdates` — parses `updates` array, applies `strict` / `whiteList` filtering, extends interactive window, and invokes user `callback(TResult)`.
   - If more Tx items remain in queue, proceeds to the next Tx; otherwise keeps socket open for Long Polling or cleanly closes socket for Short Polling.

---

## 5. Dependencies and File Structure

### Dependencies:
- **`bblanchon/ArduinoJson @ ^7.0.0`** — the only required external dependency.

### Repository Layout:
```
asyncTelegramAPI/
├── library.json           // PlatformIO manifest
├── library.properties     // Arduino IDE Library Manager manifest
├── README.md              // Russian documentation and API reference
├── README_EN.md           // English documentation and API reference
├── ARCHITECTURE.md        // English architecture specification
├── ARCHITECTURE_RU.md     // Russian architecture specification
├── src/
│   ├── AsyncTelegramAPI.h // Main non-blocking bot class
│   └── TelegramCore.h     // TResult struct, menu and JSON helpers
└── examples/
    ├── Basic_Bot/         // Universal basic example for loop()
    ├── ESP32_FreeRTOS/    // Background task on dedicated ESP32 core
    └── Interactive_Dashboard/ // Live dashboard with editMessage & callback
```
