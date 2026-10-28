# asyncTelegramAPI

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange.svg)](https://platformio.org/)
[![Framework](https://img.shields.io/badge/Framework-Arduino-blue.svg)](https://www.arduino.cc/)
[![License: LGPL 3.0](https://img.shields.io/badge/License-LGPL_3.0-green.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)](library.properties)

A fast, lightweight, and fully non-blocking Telegram Bot API library for **ESP8266** and **ESP32** via Wi-Fi.  
📖 **[Документация на русском языке (README.md)](README.md)**

If your hardware uses a GSM/LTE or Ethernet module, use the synchronous library [microTelegramAPI](https://github.com/SerhiiLe/microTelegramAPI). The API of both libraries is compatible across core methods, making it easy to switch your project between synchronous and asynchronous versions.

---

## Table of Contents

1. [Features](#features)
2. [Installation](#installation)
3. [Quick Start](#quick-start)
4. [API Reference](#api-reference)
   - [1. Initialization & Lifecycle](#1-initialization--lifecycle)
   - [2. Configuration](#2-configuration)
   - [3. Polling Modes & Interactive Window](#3-polling-modes--interactive-window)
   - [4. Sending Messages & Outbox Queue](#4-sending-messages--outbox-queue)
   - [5. Editing & Deleting](#5-editing--deleting)
   - [6. Diagnostics & Error Codes](#6-diagnostics--error-codes)
   - [7. Utility Methods](#7-utility-methods)
5. [Markup & Formatting](#markup--formatting)
   - [Auto HTML Detection](#auto-html-detection)
   - [Keyboard Menus [MENU]](#keyboard-menus-menu)
   - [Supergroup Topics](#supergroup-topics)
6. [Memory Optimization & Debugging](#memory-optimization--debugging)
7. [Migration from microTelegramAPI](#migration-from-microtelegramapi)
8. [License](#license)

---

## Features

- **Zero External Network Dependencies:**
  Runs entirely on standard system secure clients (`WiFiClientSecure` on ESP32 and `BearSSL::WiFiClientSecure` on ESP8266). Does NOT require fragile external networking libraries such as `AsyncTCP` or `ESPAsyncTCP`.
- **Fully Non-blocking Finite State Machine (FSM):**
  The `bot.tick()` method executes in under 1 microsecond when no incoming data is present. The `loop()` remains 100% free for critical time-sensitive peripherals (sensors, buttons, displays, FastLED/NeoPixel, servo motors).
- **FreeRTOS Background Mode (ESP32):**
  Optional execution in a dedicated task pinned to Core 1 (`AsyncTelegramAPI bot(true);`), completely eliminating the need to call `bot.tick()` from `loop()`.
- **Tx FIFO Outbox Queue with Preemption:**
  Holds up to 32 outgoing messages. Outgoing requests (`sendMessage`, `editMessage`, `deleteMessage`) have strict priority and immediately abort ongoing Long Polling requests to deliver outgoing messages with zero delay.
- **Smart Interactive Acceleration Window:**
  In idle standby, the bot can sleep in rare short-polling intervals (closing socket to free ~7-8 KB RAM on ESP8266), and automatically wake up into fast Long Polling upon receiving any user command (`setInteractiveWindow`).
- **Automatic HTML Parsing Detection:**
  If the message contains a closing HTML tag `</`, the library automatically sets `parse_mode = "HTML"`. No manual flags required.
- **Interactive Menu Markup [MENU]:**
  Easily create custom reply keyboards right inside message text using the `[MENU]` tag (with `\t` separating buttons within a row, and `\n` creating new rows).
- **Message Editing & Delivery Confirmation Callback:**
  Edit previously sent messages by `messageId` (ideal for live dashboards, counters, and heartbeats) and capture newly created message IDs via the `onSent` delivery callback.
- **Supergroup Topics Support:**
  The `threadId` field in `TResult` ensures that replies sent from the callback automatically target the exact topic from which the command originated.
- **RAM & Flash Optimization:**
  Format strings are stored in PROGMEM (`PSTR()`), while text constants and JSON keys use `F()`.
  Debug messages are controlled by the `#define DEBUG` macro: when disabled, all logging code and strings are completely stripped from the firmware (saving over 6 KB of Flash and precious RAM on ESP8266).

---

## Installation

### PlatformIO
Add the dependency to your `platformio.ini`:
```ini
lib_deps =
    bblanchon/ArduinoJson @ ^7.0.0
    https://github.com/SerhiiLe/asyncTelegramAPI.git
```

### Arduino IDE
1. Install **ArduinoJson** (version 7.x) via the Library Manager (*Tools -> Manage Libraries*).
2. Download the **asyncTelegramAPI** repository as a ZIP archive and extract it into your `libraries` folder (or install via *Sketch -> Include Library -> Add .ZIP Library...*).

---

## Quick Start

### Basic Example (ESP8266 & ESP32 via `loop()`):

```cpp
#include <Arduino.h>
#if defined(ESP8266)
    #include <ESP8266WiFi.h>
#elif defined(ESP32)
    #include <WiFi.h>
#endif
#include <AsyncTelegramAPI.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* pass = "YOUR_WIFI_PASSWORD";
#define BOT_TOKEN "1234567890:ABCdefGHIjklMNOpqrSTUvwxYZ"
#define CHAT_ID   123456789 // Your chat_id, or 0 to accept from anyone

AsyncTelegramAPI bot;

// Incoming message handler callback
String handleMessage(TResult& msg) {
    Serial.printf("Message from %s: %s\n", msg.from.c_str(), msg.text.c_str());

    if (bot.is_command(msg.text, F("hello"))) {
        return "Hello, " + msg.from + "!";
    }
    if (bot.is_command(msg.text, F("status"))) {
        return "<b>Status:</b>\n<code>Free RAM: " + String(ESP.getFreeHeap()) + " B</code>";
    }
    if (bot.is_command(msg.text, F("menu"))) {
        return "Main Menu:[MENU]hello\tstatus\nhelp\thide";
    }
    if (bot.is_command(msg.text, F("hide"))) {
        return "Menu hidden[MENU]";
    }

    // Return an empty string if no reply is needed
    return "";
}

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");

    bot.setBotToken(BOT_TOKEN);
    if (CHAT_ID != 0) bot.setChatID(CHAT_ID);
    bot.attachCallback(handleMessage);

    // Default standby: check every 90 seconds, socket is closed between polls
    bot.setLongPoll(0);
    bot.setInterval(90);

    // On user activity, switch to fast Long Polling (20s) for 3 minutes:
    bot.setInteractiveWindow(180, 20);

    // Start bot FSM
    bot.start();

    if (CHAT_ID != 0) {
        bot.sendMessage("<b>Bot started successfully!</b>");
    }
}

void loop() {
    // Non-blocking state machine step (< 1 us when idle)
    bot.tick();

    // Your own non-blocking application tasks
}
```

---

### FreeRTOS Background Task Mode (ESP32):

```cpp
#include <WiFi.h>
#include <AsyncTelegramAPI.h>

// Pass true to the constructor: FSM runs in a dedicated FreeRTOS task
AsyncTelegramAPI bot(true);

String handleMessage(TResult& msg) {
    if (bot.is_command(msg.text, F("ping"))) {
        return "pong!";
    }
    return "";
}

void setup() {
    Serial.begin(115200);
    WiFi.begin("WIFI_SSID", "WIFI_PASS");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    bot.setBotToken("BOT_TOKEN");
    bot.attachCallback(handleMessage);
    bot.setLongPoll(25); // Continuous 25s Long Polling

    // Start bot and background task (core=1, stackSize=8192, priority=1)
    bot.start();
}

void loop() {
    // loop() is 100% free! No need to call bot.tick().
    delay(1000);
}
```

---

## API Reference

### 1. Initialization & Lifecycle

| Method | Description |
| :--- | :--- |
| `AsyncTelegramAPI(bool useTask = false)` | Constructor. On ESP32, `useTask=true` runs the bot FSM in a dedicated background FreeRTOS task. |
| `~AsyncTelegramAPI()` | Destructor. Safely halts network activity, stops the FreeRTOS task, and closes sockets. |
| `bool start(BaseType_t core = 1, uint32_t stackSize = 8192, UBaseType_t priority = 1)` | *(ESP32)* Start bot networking and optional FreeRTOS worker on specified core. |
| `bool start(...)` | *(ESP8266)* Start bot networking. |
| `bool stop()` | Completely halt bot activity, flush queue, and stop the FreeRTOS task. |
| `void tick()` | Primary non-blocking FSM step. Called in `loop()` when FreeRTOS task mode is not used. |
| `int checkMessage(bool force = false)` | Manual poll check. Returns number of received updates since last call (>= 0) or error code (< 0). |
| `bool isStarted() const` | Check whether the bot is started via `start()`. |
| `bool isReady() const` | Check whether the bot is ready for immediate message delivery (started, Wi-Fi connected, idle socket, not paused). |
| `bool isTaskRunning() const` | *(ESP32)* Check whether the FreeRTOS background task is actively running. |
| `bool isTaskMode() const` | Check whether FreeRTOS task mode is enabled on this bot instance. |

---

### 2. Configuration

| Method / Property | Description |
| :--- | :--- |
| `void setBotToken(const String& token)`<br>`void setBotToken(const char* token)` | Set the Telegram Bot API token. |
| `void setChatID(int64_t chatId)` | Set default chat ID (used when sending messages without explicitly specifying `chatId`). |
| `void setThreadID(int64_t threadId)` | Set default forum topic / thread ID for supergroups. |
| `String whiteList` | Public property: comma-separated list of allowed chat IDs (e.g. `"111617414, -100123456789"`). |
| `bool strict` | Public property: access control mode. If `true`, the bot responds **only** to messages from default chat (`_chatId`) or IDs listed in `whiteList`. If `false` (default), the bot accepts messages from anyone. |
| `void attachCallback(CallbackType cb)` | Attach message handler callback: `String callback(TResult& msg)`. Non-empty returned string is automatically sent as a reply to the same chat and topic. |

---

### 3. Polling Modes & Interactive Window

The library supports three distinct polling modes:

1. **Long Polling (`setLongPoll(seconds)`):**
   Holds open HTTP connection from 10 to 50 seconds. Incoming messages are delivered immediately with zero latency.
2. **Short Polling (`setInterval(seconds)`):**
   Periodic polling. The socket is cleanly closed between requests, freeing BearSSL RAM (~7-8 KB) on ESP8266.
3. **Smart Interactive Window (`setInteractiveWindow`):**
   Hybrid approach: bot rests in rare short-polling intervals (e.g. every 5–10 minutes) and automatically accelerates to fast Long Polling when commands arrive, resetting the countdown timer on each message.

| Method | Description |
| :--- | :--- |
| `void setLongPoll(uint8_t timeoutSeconds)` | Set Long Polling timeout (10 to 50 seconds). `0` disables Long Polling (switches to short polling). Disables the interactive window. |
| `void setInterval(uint32_t intervalSeconds)` | Set short polling interval in seconds (e.g. `60` or `300`). Disables the Long Polling and interactive window. |
| `void setInteractiveWindow(uint32_t windowSeconds, uint8_t fastLongPollTimeout = 20, uint16_t slowShortPollTimeout = 120)` | Enable interactive acceleration. On incoming commands, switches to fast Long Polling (`fastLongPollTimeout`) for `windowSeconds` before reverting to Short Polling (`slowShortPollTimeout`). Disables both Long and Short Polling. |
| `void triggerInteractive()` | Forcefully activate the interactive window. |
| `bool isInteractive() const` | Check whether the interactive window is currently active. |
| `uint32_t getInteractiveRemaining() const` | Remaining time of the interactive window (in seconds). |
| `void setNext(uint32_t seconds = 0)` | Pause all network requests for `seconds` seconds. `0` cancels pause. |
| `bool isPaused() const` | Check whether network requests are currently paused by `setNext`. |
| `uint32_t getNextRemaining() const` | Remaining pause duration in seconds. |

---

### 4. Sending Messages & Outbox Queue

All send methods are non-blocking and push requests to the internal `TxItem` queue (up to 32 items):

| Method | Description |
| :--- | :--- |
| `sendMessage(chatId, text, [threadId], [onSent])` | Send message to specified chat with optional topic (`threadId`) and delivery confirmation callback `onSent`. |
| `sendMessage(text, [threadId], [onSent])` | Send message to default chat (`_chatId`). |
| `sendMessageToAll([whiteList], text, [threadId])` | Broadcast message to all IDs in the whitelist (uses `bot.whiteList` if argument is omitted). |
| `size_t getQueueSize() const` | Current number of messages waiting in the outgoing queue. |

#### Using `onSent` Delivery Callback:
```cpp
bot.sendMessage("Temperature: 24.5 C", [](int64_t msgId) {
    if (msgId != 0) {
        Serial.printf("Message delivered, ID: %lld\n", (long long)msgId);
    } else {
        Serial.println("Message delivery failed!");
    }
});
```

> 💡 **Configuring Queue Capacity (`QUEUE_SIZE`)**  
> By default, the outbound transmission queue limit is `32` messages (`const size_t MAX_TX_QUEUE = QUEUE_SIZE`).  
> You can customize this threshold to suit your hardware resources:
> - In code prior to including the header:
>   ```cpp
>   #define QUEUE_SIZE 16
>   #include <AsyncTelegramAPI.h>
>   ```
> - Or in `platformio.ini` via build flags:
>   ```ini
>   build_flags = -DQUEUE_SIZE=16
>   ```
> **Memory Consumption:**  
> The queue utilizes `std::queue` (`std::deque`) and allocates memory dynamically only as items are enqueued (an empty queue requires only ~40 bytes). A single queued message uses approximately **~120 bytes of RAM** (`TxItem` struct ~64 bytes + dynamic string buffer). At maximum default capacity (32 items), peak heap consumption is approximately **~3.8 KB RAM**. On memory-constrained devices (such as ESP8266), the queue size can be safely lowered to `8`–`16`.

---

### 5. Editing & Deleting

| Method | Description |
| :--- | :--- |
| `editMessage(chatId, messageId, text, [onSent])` | Edit text of an existing message by its `messageId` in specified chat. |
| `editMessage(messageId, text, [onSent])` | Edit text of an existing message in default chat. |
| `deleteMessage(chatId, messageId)` | Delete a single message by `messageId` in specified chat. |
| `deleteMessage(messageId)` | Delete a single message in default chat. |
| `deleteMessages(chatId, {id1, id2, ...})` | Batch delete multiple messages in specified chat in a single API request (`deleteMessages`). |
| `deleteMessages({id1, id2, ...})` | Batch delete multiple messages in default chat. |

---

### 6. Diagnostics & Error Codes

| Method | Description |
| :--- | :--- |
| `int getLastError() const` | Returns code of the last error from `TgError` enum. |
| `const __FlashStringHelper* getLastErrorText() const` | Returns textual description of the last error directly from Flash memory (PROGMEM). |
| `static const __FlashStringHelper* getErrorText(int code)` | Static text lookup for any error code. |

#### Error Codes (`TgError`):
- `TG_OK (0)` — No errors / last operation succeeded
- `TG_ERR_CONNECTION (-1)` — Failed to connect to `api.telegram.org:443` (DNS, TCP, TLS)
- `TG_ERR_NO_WIFI (-2)` — Wi-Fi is not connected
- `TG_ERR_NO_TOKEN (-3)` — Bot token is empty or shorter than 30 characters
- `TG_ERR_TIMEOUT (-4)` — Request timeout waiting for Telegram server response
- `TG_ERR_READ_TIMEOUT (-5)` — Read timeout or connection dropped while receiving data
- `TG_ERR_HTTP (-6)` — HTTP status code is not 200 (e.g. 400, 500, 502)
- `TG_ERR_RATE_LIMIT (-7)` — HTTP 429 Too Many Requests (rate limit backoff applied)
- `TG_ERR_PARSE (-8)` — JSON deserialization error
- `TG_ERR_API (-9)` — Telegram API reported failure (`ok: false`)
- `TG_ERR_QUEUE_FULL (-10)` — Outgoing message queue is full (max 32 items)

---

### 7. Utility Methods

| Method | Description |
| :--- | :--- |
| `static bool is_command(const String& in, const String& command)` | Check if message starts with command, with or without leading `/` (e.g. `"start"` matches both `"/start"` and `"start"`). |
| `bool isWhitelisted(int64_t chatId)` | Check whether `chatId` is present in this bot instance whitelist (`bot.whiteList`). |
| `static bool isWhitelisted(int64_t chatId, const String& whiteList)` | Check whether `chatId` is present in a given comma-separated whitelist string. |
| `int64_t extractFirstNumber()` | Extract first numeric ID from this bot instance whitelist (`bot.whiteList`). |
| `static int64_t extractFirstNumber(const String& whiteList)` | Extract first numeric ID from a given comma-separated string. |

---

## Markup & Formatting

### Auto HTML Detection
The library automatically enables HTML parsing if the message text contains a closing HTML tag `</`:
```cpp
bot.sendMessage("<b>Bold</b>, <i>italic</i>, <code>code</code>, <a href=\"https://github.com\">link</a>");
```

### Keyboard Menus `[MENU]`
Reply keyboards are embedded directly into message text using the `[MENU]` tag:
- `\t` — separates buttons on the same row
- `\n` — starts a new row of buttons
- An empty `[MENU]` tag at the end hides/removes the keyboard

```cpp
// Create a 2-row reply keyboard:
bot.sendMessage("Select action:[MENU]Light ON\tLight OFF\nStatus\tSettings");

// Hide reply keyboard:
bot.sendMessage("Keyboard hidden[MENU]");
```

### Supergroup Topics
The `threadId` field is included in the `TResult` structure:
```cpp
struct TResult {
    int64_t chatId;
    int64_t messageId;
    int64_t threadId; // Supergroup topic ID (0 if private chat or general forum)
    String text;
    String from;
};
```
When a command originates from a topic, returning a string from `callback` automatically routes the response to that same topic. To explicitly target a topic, pass `threadId` as the 3rd argument:
```cpp
bot.sendMessage(chatId, "Alert in topic!", topicId);
```

---

## Memory Optimization & Debugging

To enable verbose debug output to `Serial`, define `#define DEBUG` **before including the library** in your sketch:

```cpp
#define DEBUG
#include <AsyncTelegramAPI.h>
```

### In Standard Production Mode (without `#define DEBUG`):
- Macro `LOG_TELEGRAM_API(...)` evaluates to an empty statement.
- The compiler strips 100% of debug format strings and checks from the binary.
- **Savings:** On ESP8266, firmware size is reduced by over **6.1 KB Flash**, and 0 bytes of RAM are consumed by debug string constants.
- All internal string constants and JSON keys are stored directly in Flash memory via `F()` and `PSTR()`.

---

## Migration from microTelegramAPI

Both libraries share the same core methods, so adapting your existing sketch code should be straightforward. However, there are several key differences to keep in mind:
- **Code Size & Complexity:** The code of `asyncTelegramAPI` is more complex and roughly twice the size of `microTelegramAPI` (~700 additional lines of code and ~2.5 KB added to the compiled sketch size).
- **RAM Overhead for Long Polling:** To operate in Long Polling mode (which ensures instant reaction to incoming messages), the system must keep approximately 9 KB in RAM for active TLS connection and socket buffers. On ESP8266, you should monitor your free heap carefully.
- **Continuous Execution & Lifecycle:** Unlike `microTelegramAPI`, `bot.tick()` must be called continuously inside `loop()` (unless running in ESP32 FreeRTOS task mode), rather than only when you expect an event. To initiate the polling and dispatch lifecycle, calling `bot.start()` is mandatory; without it, the request and response lifecycle is not formed. In `microTelegramAPI`, there is no internal background loop — requests execute synchronously whenever you invoke them.
- **Automatic Connection Tracking:** The library tracks Internet connectivity automatically, so you do not need to manually restrict polling when disconnected.
- **Built-in Outbox Queue:** Because the library is fully asynchronous, both receiving and sending use an internal 32-message queue (including on ESP8266). You do not need to implement custom outgoing queues.
- **Minor API Differences:** Although most methods share compatible syntax and usage patterns, minor differences exist. When in doubt, consult this documentation and the provided examples.

`asyncTelegramAPI` is designed to be faster and more convenient on ESP8266 and ESP32 over Wi-Fi. However, `microTelegramAPI` is lighter and truly transport-agnostic (working over GSM, LTE, Ethernet, etc.). They serve different use cases, while sharing a common philosophy: maximum simplicity without unnecessary bloat that is never used in real-world IoT systems.

---

## License

This library is licensed under the **LGPL-3.0** License. You are free to use it in open-source and commercial microcontroller projects.
