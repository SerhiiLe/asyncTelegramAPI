# asyncTelegramAPI

A fast, lightweight, and fully non-blocking Telegram Bot API library for **ESP8266** and **ESP32** via Wi-Fi.

Быстрая, легковесная и полностью неблокирующая библиотека Telegram Bot API для **ESP8266** и **ESP32** при работе через Wi-Fi.

---

## Table of Contents / Оглавление

1. [Features / Особенности](#features--особенности)
2. [Installation / Установка](#installation--установка)
3. [Quick Start / Быстрый старт](#quick-start--быстрый-старт)
4. [API Reference / Описание методов API](#api-reference--описание-методов-api)
   - [Initialization & Lifecycle / Инициализация и запуск](#1-initialization--lifecycle--инициализация-и-запуск)
   - [Configuration / Настройки бота](#2-configuration--настройки-бота)
   - [Polling & Interactive Window / Режимы опроса и интерактивное окно](#3-polling--interactive-window--режимы-опроса-и-интерактивное-окно)
   - [Sending & Queueing / Отправка сообщений и очередь](#4-sending--queueing--отправка-сообщений-и-очередь)
   - [Editing & Deleting / Редактирование и удаление](#5-editing--deleting--редактирование-и-удаление)
   - [Diagnostics & Errors / Диагностика и ошибки](#6-diagnostics--errors--диагностика-и-ошибки)
   - [Utility Methods / Вспомогательные методы](#7-utility-methods--вспомогательные-методы)
5. [Markup & Formatting / Разметка и меню](#markup--formatting--разметка-и-меню)
   - [Auto HTML / Автоопределение HTML](#auto-html--автоопределение-html)
   - [Menu Keyboard / Меню клавиатуры [MENU]](#menu-keyboard--меню-клавиатуры-menu)
   - [Topics / Топики и темы супергрупп](#topics--топики-и-темы-супергрупп)
6. [Memory Optimization & Debug / Оптимизация памяти и отладка](#memory-optimization--debug--оптимизация-памяти-и-отладка)
7. [License / Лицензия](#license--лицензия)

---

## Features / Особенности

- **Zero External Network Dependencies / Без сторонних сетевых библиотек:**
  Работает на стандартных системных клиентах (`WiFiClientSecure` на ESP32 и `BearSSL::WiFiClientSecure` на ESP8266). Не требует проблемных внешних библиотек `AsyncTCP` или `ESPAsyncTCP`.
- **Non-blocking FSM / Полностью неблокирующий автомат:**
  Метод `bot.tick()` выполняется менее чем за 1 микросекунду при отсутствии входящих байт. Ваш цикл `loop()` свободен на 100% для критических задач (датчики, кнопки, дисплеи, FastLED/NeoPixel, сервоприводы).
- **FreeRTOS Background Mode / Фоновый режим FreeRTOS (ESP32):**
  Опциональный запуск автомата бота в отдельной задаче на втором ядре (`AsyncTelegramAPI bot(true);`), полностью освобождающий `loop()` от необходимости вызова `bot.tick()`.
- **Tx FIFO Outbox Queue / Очередь отправки с приоритетом:**
  Очередь до 32 исходящих сообщений. Исходящие запросы (`sendMessage`, `editMessage`, `deleteMessage`) имеют абсолютный приоритет и мгновенно прерывают текущий входящий poll, обеспечивая моментальную реакцию.
- **Smart Interactive Window / Интерактивное окно ускорения:**
  В режиме ожидания бот может спать в редком short-polling (раз в несколько минут с закрытием сокета, освобождая ~7-8 КБ RAM на ESP8266), а при получении команды автоматически просыпаться в сверхбыстрый Long Polling на заданное время (`setInteractiveWindow`).
- **Auto HTML Detection / Автоопределение HTML:**
  Если в тексте сообщения найден закрывающий тег `</`, библиотека автоматически включает режим `parse_mode = "HTML"`. Ручное указание параметров больше не требуется.
- **Interactive Menu Markup / Разметка меню [MENU]:**
  Создание и скрытие экранных клавиатур прямо в тексте сообщения тегом `[MENU]` (с разделителями `\t` для кнопок в строке и `\n` для переноса строки).
- **Edit Message & Delivery Callback / Редактирование сообщений и callback:**
  Возможность редактировать ранее отправленные сообщения по `messageId` (удобно для живых дашбордов, счетчиков и heartbeat), а также получение ID отправленного сообщения через callback `onSent`.
- **Supergroup Topics / Поддержка топиков супергрупп:**
  Поле `threadId` в структуре `TResult` и автоматическая отправка ответов именно в тот топик, откуда пришла команда.
- **RAM & Flash Optimization / Максимальная экономия памяти:**
  Все строки форматирования упакованы в `PSTR()`, текстовые константы и JSON-ключи — в `F()`.
  Отладочные сообщения управляются макросом `#define DEBUG`: при отключении отладки все строки логов и отладочный код на 100% вырезаются из бинарника (экономия более 6 КБ Flash и ценной RAM).

---

## Installation / Установка

### PlatformIO
Добавьте зависимость в ваш `platformio.ini`:
```ini
lib_deps =
    bblanchon/ArduinoJson @ ^7.0.0
    https://github.com/SerhiiLe/asyncTelegramAPI.git
```

### Arduino IDE
1. Установите библиотеку **ArduinoJson** (версия 7.x) через Менеджер библиотек (*Tools -> Manage Libraries*).
2. Скачайте репозиторий **asyncTelegramAPI** в виде ZIP-архива и распакуйте его в папку `libraries` (или установите через *Sketch -> Include Library -> Add .ZIP Library...*).

---

## Quick Start / Быстрый старт

### Базовый пример (ESP8266 и ESP32 через `loop()`):

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
#define CHAT_ID   123456789 // ID вашего чата или 0 для приема от любых

AsyncTelegramAPI bot;

// Callback входящих сообщений
String handleMessage(TResult& msg) {
    Serial.printf("Сообщение от %s: %s\n", msg.from.c_str(), msg.text.c_str());

    if (bot.is_command(msg.text, F("hello"))) {
        return "Привет, " + msg.from + "!";
    }
    if (bot.is_command(msg.text, F("status"))) {
        return "<b>Статус:</b>\n<code>Free RAM: " + String(ESP.getFreeHeap()) + " B</code>";
    }
    if (bot.is_command(msg.text, F("menu"))) {
        return "Главное меню:[MENU]hello\tstatus\nhelp\thide";
    }
    if (bot.is_command(msg.text, F("hide"))) {
        return "Меню скрыто[MENU]";
    }

    // Возврат пустой строки, если отвечать не требуется
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

    // Базовый режим: опрос раз в 90 сек, сокет закрывается
    bot.setLongPoll(0);
    bot.setInterval(90);

    // При активности в чате включаем быстрый Long Polling (20 сек) на 3 минуты:
    bot.setInteractiveWindow(180, 20);

    // Запуск работы автомата бота
    bot.start();

    if (CHAT_ID != 0) {
        bot.sendMessage("<b>Бот успешно запущен!</b>");
    }
}

void loop() {
    // Неблокирующий шаг автомата (< 1 мкс в покое)
    bot.tick();

    // Другие ваши неблокирующие задачи
}
```

---

### Фоновый режим FreeRTOS (ESP32):

```cpp
#include <WiFi.h>
#include <AsyncTelegramAPI.h>

// Передаем true в конструктор: автомат запускается в отдельной задаче FreeRTOS
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
    bot.setLongPoll(25); // Постоянный Long Polling 25 секунд

    // Запуск бота и фоновой задачи (core=1, stackSize=8192, priority=1)
    bot.start();
}

void loop() {
    // Цикл loop() свободен на 100%! Вызывать bot.tick() не нужно.
    delay(1000);
}
```

---

## API Reference / Описание методов API

### 1. Initialization & Lifecycle / Инициализация и запуск

| Метод | Описание |
| :--- | :--- |
| `AsyncTelegramAPI(bool useTask = false)` | Конструктор. На ESP32 параметр `useTask=true` включает обслуживание бота в отдельной фоновой задаче FreeRTOS. |
| `~AsyncTelegramAPI()` | Деструктор. Корректно останавливает сетевую активность, задачу FreeRTOS и закрывает сокеты. |
| `bool start(BaseType_t core = 1, uint32_t stackSize = 8192, UBaseType_t priority = 1)` | *(ESP32)* Запуск работы бота с сетью и опциональной задачи FreeRTOS на указанном ядре процессора. |
| `bool start(...)` | *(ESP8266)* Запуск работы бота с сетью. |
| `bool stop()` | Полная остановка сетевой активности бота, очистка очереди и завершение задачи FreeRTOS. |
| `void tick()` | Главный неблокирующий шаг автомата FSM. Вызывается в `loop()`, если не используется режим задачи FreeRTOS. |
| `int checkMessage(bool force = false)` | Ручная проверка новых сообщений. Возвращает число полученных сообщений (> 0) или код ошибки (< 0). |
| `bool isStarted() const` | Проверка, запущен ли бот вызовом `start()`. |
| `bool isReady() const` | Готов ли бот к немедленной отправке сообщений (запущен, Wi-Fi подключен, сокет в покое, нет паузы). |
| `bool isTaskRunning() const` | *(ESP32)* Проверка, активна ли сейчас фоновая задача FreeRTOS. |
| `bool isTaskMode() const` | Проверка, включен ли режим задачи FreeRTOS в объекте бота. |

---

### 2. Configuration / Настройки бота

| Метод | Описание |
| :--- | :--- |
| `void setBotToken(const String& token)`<br>`void setBotToken(const char* token)` | Установка токена Telegram бота. |
| `void setChatID(int64_t chatId)` | Установка ID чата по умолчанию (для методов отправки без явного указания чата). |
| `void setThreadID(int64_t threadId)` | Установка ID топика/темы по умолчанию для супергрупп. |
| `String whiteList` | Публичное поле: белый список разрешенных ID чатов через запятую (например: `"111617414, -100123456789"`). |
| `bool strict` | Публичное поле: строгий режим доступа. Если `true`, бот отвечает **только** на сообщения от чата по умолчанию (`_chatId`) или из белого списка (`whiteList`). Если `false` (по умолчанию) — бот отвечает на любые входящие запросы. |
| `void attachCallback(CallbackType cb)` | Привязка функции-обработчика входящих сообщений: `String callback(TResult& msg)`. То, что возвращает callback (если не пустая строка), автоматически отправляется ответом в тот же чат и тред. |

---

### 3. Polling & Interactive Window / Режимы опроса и интерактивное окно

Библиотека поддерживает три гибких режима опроса:

1. **Long Polling (`setLongPoll(секунды)`):**
   Держит соединение с Telegram открытым от 10 до 50 секунд. Сообщения приходят мгновенно без задержки.
2. **Short Polling (`setInterval(секунды)`):**
   Периодический опрос. Между запросами сокет закрывается, освобождая оперативную память BearSSL (~7-8 КБ) на ESP8266.
3. **Smart Interactive Window (`setInteractiveWindow`):**
   Идеальный гибрид! В покое бот находится в редком short-polling (например, раз в 5-10 минут), а при получении команды переходит в быстрый Long Polling на заданное время, продлевая его при каждом новом сообщении.

| Метод | Описание |
| :--- | :--- |
| `void setLongPoll(uint8_t timeoutSeconds)` | Установка таймаута Long Polling (от 10 до 50 секунд). `0` отключает Long Polling (переход на short polling). |
| `void setInterval(uint32_t intervalSeconds)` | Период опроса для short polling в секундах (например, `60` или `300`). |
| `void setInteractiveWindow(uint32_t windowSeconds, uint8_t fastLongPollTimeout = 20)` | Включение интерактивного окна. На `windowSeconds` секунд после входящей команды бот переходит в быстрый Long Polling (`fastLongPollTimeout`). |
| `bool isInteractive() const` | Проверка, активно ли сейчас интерактивное окно ускорения. |
| `uint32_t getInteractiveRemaining() const` | Оставшееся время активности интерактивного окна (в секундах). |
| `void setNext(uint32_t seconds = 0)` | Разовая пауза сетевых запросов на `seconds` секунд (например, чтобы дать передышку датчикам или радиомодулю). `0` — отмена паузы. |
| `bool isPaused() const` | Находится ли бот на паузе по `setNext`. |
| `uint32_t getNextRemaining() const` | Сколько секунд паузы осталось. |

---

### 4. Sending & Queueing / Отправка сообщений и очередь

Все методы отправки неблокирующие и помещают запрос в очередь `TxItem` (до 32 элементов). Поддерживаются перегрузки как для указанного чата, так и для чата по умолчанию, с поддержкой топиков и callback фактической доставки:

| Метод | Описание |
| :--- | :--- |
| `sendMessage(chatId, text, [threadId], [onSent])` | Отправка сообщения в указанный чат с опциональным указанием темы (`threadId`) и callback доставки `onSent`. |
| `sendMessage(text, [threadId], [onSent])` | Отправка сообщения в чат по умолчанию (`_chatId`). |
| `sendMessageToAll([whiteList], text, [threadId])` | Массовая рассылка сообщения по белому списку (если аргумент `whiteList` опущен, рассылка выполняется по `bot.whiteList`). |
| `size_t getQueueSize() const` | Текущее количество сообщений в очереди на отправку. |

#### Использование `onSent` Callback:
```cpp
bot.sendMessage("Температура: 24.5 C", [](int64_t msgId) {
    if (msgId != 0) {
        Serial.printf("Сообщение успешно доставлено, ID: %lld\n", (long long)msgId);
    } else {
        Serial.println("Ошибка отправки сообщения!");
    }
});
```

---

### 5. Editing & Deleting / Редактирование и удаление

| Метод | Описание |
| :--- | :--- |
| `editMessage(chatId, messageId, text, [onSent])` | Редактирование текста сообщения по его `messageId` в указанном чате. |
| `editMessage(messageId, text, [onSent])` | Редактирование сообщения в чате по умолчанию. |
| `deleteMessage(chatId, messageId)` | Удаление сообщения по `messageId` в указанном чате. |
| `deleteMessage(messageId)` | Удаление сообщения в чате по умолчанию. |
| `deleteMessages(chatId, {id1, id2, ...})` | Групповое удаление сообщений в указанном чате за один запрос API (`deleteMessages`). |
| `deleteMessages({id1, id2, ...})` | Групповое удаление сообщений в чате по умолчанию. |

---

### 6. Diagnostics & Errors / Диагностика и ошибки

| Метод | Описание |
| :--- | :--- |
| `int getLastError() const` | Возвращает код последней ошибки из перечисления `TgError`. |
| `const __FlashStringHelper* getLastErrorText() const` | Возвращает текстовое описание последней ошибки прямо из Flash памяти (PROGMEM). |
| `static const __FlashStringHelper* getErrorText(int code)` | Статическая расшифровка любого кода ошибки. |

#### Коды ошибок `TgError`:
- `TG_OK (0)` — Ошибок нет / последняя операция успешна
- `TG_ERR_CONNECTION (-1)` — Сбой подключения к `api.telegram.org:443` (DNS, TCP, TLS)
- `TG_ERR_NO_WIFI (-2)` — Нет подключения к Wi-Fi сети
- `TG_ERR_NO_TOKEN (-3)` — Токен бота не задан или короче 30 символов
- `TG_ERR_TIMEOUT (-4)` — Таймаут ожидания ответа от сервера Telegram
- `TG_ERR_READ_TIMEOUT (-5)` — Таймаут или обрыв соединения при чтении данных
- `TG_ERR_HTTP (-6)` — Код ответа HTTP отличен от 200 (например, 400, 500, 502)
- `TG_ERR_RATE_LIMIT (-7)` — Превышен лимит запросов (HTTP 429 Too Many Requests)
- `TG_ERR_PARSE (-8)` — Ошибка парсинга JSON ответа
- `TG_ERR_API (-9)` — Ошибка ответа Telegram API (`ok: false`)
- `TG_ERR_QUEUE_FULL (-10)` — Очередь исходящих сообщений переполнена (максимум 32)

---

### 7. Utility Methods / Вспомогательные методы

| Метод | Описание |
| :--- | :--- |
| `static bool is_command(const String& in, const String& command)` | Поиск команды в начале текста с учетом или без символа `/` (например: `"start"` найдет и `"/start"`, и `"start"`). |
| `bool isWhitelisted(int64_t chatId)` | Проверка, входит ли `chatId` в белый список текущего бота (`bot.whiteList`). |
| `static bool isWhitelisted(int64_t chatId, const String& whiteList)` | Проверка наличия `chatId` в переданной строке белого списка. |
| `int64_t extractFirstNumber()` | Извлечение первого числового ID из белого списка текущего бота (`bot.whiteList`). |
| `static int64_t extractFirstNumber(const String& whiteList)` | Извлечение первого числового ID из переданной строки белого списка. |

---

## Markup & Formatting / Разметка и меню

### Auto HTML / Автоопределение HTML
Библиотека автоматически включает режим HTML-разметки, если в тексте сообщения встречается закрывающий тег `</`:
```cpp
bot.sendMessage("<b>Жирный</b>, <i>курсив</i>, <code>код</code>, <a href=\"https://github.com\">ссылка</a>");
```

### Menu Keyboard / Меню клавиатуры `[MENU]`
Клавиатура создается прямо внутри текста сообщения с помощью тега `[MENU]`:
- `\t` — разделяет кнопки в одной строке
- `\n` — перенос на новую строку кнопок
- Пустой тег `[MENU]` в конце текста скрывает клавиатуру

```cpp
// Создание меню из 2 строк:
bot.sendMessage("Выберите действие:[MENU]Свет ВКЛ\tСвет ВЫКЛ\nСтатус\tНастройки");

// Скрытие клавиатуры:
bot.sendMessage("Меню скрыто[MENU]");
```

### Topics / Топики и темы супергрупп
В структуре `TResult` передается поле `threadId`:
```cpp
struct TResult {
    int64_t chatId;
    int64_t messageId;
    int64_t threadId; // ID топика супергруппы (0, если личный чат или без топика)
    String text;
    String from;
};
```
Если сообщение пришло из топика супергруппы, ответ через `return "..."` из функции `callback` автоматически отправляется в этот же топик. Для явной отправки в топик передавайте `threadId` третьим аргументом:
```cpp
bot.sendMessage(chatId, "Тревога в топике!", topicId);
```

---

## Memory Optimization & Debug / Оптимизация памяти и отладка

Для включения подробного отладочного вывода библиотеки в `Serial` добавьте `#define DEBUG` **перед подключением библиотеки** в скетче:

```cpp
#define DEBUG
#include <AsyncTelegramAPI.h>
```

### В боевом режиме (без `#define DEBUG`):
- Макрос `LOG_TELEGRAM_API(...)` превращается в пустую операцию.
- Компилятор вырезает абсолютно все отладочные строки и проверки из прошивки.
- **Экономия:** на ESP8266 прошивка уменьшается более чем на **6.1 КБ Flash**, а в оперативной памяти (RAM) не создается ни одной строки отладочного текста.
- Все системные строковые константы и JSON-ключи помещены во Flash память с помощью `F()` и `PSTR()`.

---

## License / Лицензия

Библиотека распространяется под лицензией **LGPL-3.0**. Вы можете свободно использовать её в своих открытых и коммерческих проектах для микроконтроллеров.
