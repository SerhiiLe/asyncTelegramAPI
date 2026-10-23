#ifndef ASYNC_TELEGRAM_API_H
#define ASYNC_TELEGRAM_API_H

#include <Arduino.h>
#include <queue>
#include <vector>
#include "TelegramCore.h"

#if defined(ESP8266)
    #include <ESP8266WiFi.h>
    #include <WiFiClientSecure.h>
    #include <WiFiClientSecureBearSSL.h>
    using TelegramClient = BearSSL::WiFiClientSecure;
#elif defined(ESP32)
    #include <WiFi.h>
    #include <WiFiClientSecure.h>
    #include <freertos/FreeRTOS.h>
    #include <freertos/task.h>
    using TelegramClient = WiFiClientSecure;
#else
    #error "Platform not supported! Only ESP8266 and ESP32 are supported."
#endif

#ifdef LOG_TELEGRAM_API
#undef LOG_TELEGRAM_API
#endif

#if defined(DEBUG) || defined(TG_DEBUG)
    #define LOG_TELEGRAM_API(func, ...) Serial.func(__VA_ARGS__)
#else
    #define LOG_TELEGRAM_API(func, ...) ;
#endif

// Коды ошибок для getLastError() и возврата checkMessage()
enum TgError {
    TG_OK = 0,                 // Ошибок нет / последняя операция успешна
    TG_ERR_CONNECTION = -1,    // Сбой подключения к api.telegram.org (DNS, TCP, TLS)
    TG_ERR_NO_WIFI = -2,       // Нет подключения к Wi-Fi
    TG_ERR_NO_TOKEN = -3,      // Токен бота не задан или короче 30 символов
    TG_ERR_TIMEOUT = -4,       // Таймаут ожидания ответа от сервера (Wait Timeout)
    TG_ERR_READ_TIMEOUT = -5,  // Таймаут / обрыв связи при получении данных ответа
    TG_ERR_HTTP = -6,          // Ошибка HTTP (код ответа не 200, например 400, 500, 502)
    TG_ERR_RATE_LIMIT = -7,    // Превышен лимит запросов (HTTP 429 Too Many Requests)
    TG_ERR_PARSE = -8,         // Ошибка парсинга JSON ответа
    TG_ERR_API = -9,           // Ошибка Telegram API (doc["ok"] == false)
    TG_ERR_QUEUE_FULL = -10    // Исходящая очередь сообщений переполнена
};

class AsyncTelegramAPI {
public:
    using CallbackType = std::function<String(TResult&)>;
    using OnSentCallback = std::function<void(int64_t messageId)>;

    // Строгий режим: если true, бот принимает сообщения только от _chatId или из белого списка whiteList.
    // Если false (по умолчанию) — бот отвечает на любой входящий запрос от любого пользователя.
    bool strict = false;

    // Белый список разрешенных ID чатов через запятую (например: "111617414, -100123456789")
    String whiteList;

    // Конструктор бота. На ESP32 параметр useTask=true запускает автомат в отдельной фоновой задаче FreeRTOS
    AsyncTelegramAPI(bool useTask = false) : _client() {
#if defined(ESP32)
        _useTask = useTask;
#else
        (void)useTask;
#endif
        _client.setInsecure();
#if defined(ESP8266)
        _client.setBufferSizes(1024, 512);
#endif
    }

    // Деструктор бота. Завершает работу, останавливает задачу FreeRTOS и закрывает соединения
    ~AsyncTelegramAPI() {
        stop();
    }

    // --- Управление работой бота (start / stop) ---
#if defined(ESP32)
    // Запуск работы бота с сетью (и фоновой задачи FreeRTOS, если включен useTask)
    bool start(BaseType_t core = 1, uint32_t stackSize = 8192, UBaseType_t priority = 1) {
        _started = true;
        _state = TG_IDLE;
        _lastPollEndTime = 0;
        _lastConnectAttempt = 0;
        _lastError = TG_OK;
        _tickStatus = 0;
        _forcePoll = (_longPollTimeout > 0 || _pollInterval > 0);
        _pauseUntil = 0;
        _activeUntil = 0;

        if (_useTask) {
            if (_taskHandle != nullptr) return true; // Уже запущена

            _running = true;
            BaseType_t res = xTaskCreatePinnedToCore(
                _taskWorker,
                "TgTask",
                stackSize,
                this,
                priority,
                &_taskHandle,
                core
            );

            if (res != pdPASS) {
                _running = false;
                _taskHandle = nullptr;
                return false;
            }
        }
        return true;
    }
#else
    // Запуск работы бота с сетью на ESP8266
    bool start(int core = 1, uint32_t stackSize = 8192, int priority = 1) {
        (void)core; (void)stackSize; (void)priority;
        _started = true;
        _state = TG_IDLE;
        _lastPollEndTime = 0;
        _lastConnectAttempt = 0;
        _lastError = TG_OK;
        _tickStatus = 0;
        _forcePoll = (_longPollTimeout > 0 || _pollInterval > 0);
        _pauseUntil = 0;
        _activeUntil = 0;
        return true;
    }
#endif

    // Полная остановка работы бота (сеть, сокет, задача FreeRTOS)
    bool stop() {
        _started = false;

#if defined(ESP32)
        if (_taskHandle != nullptr) {
            _running = false;
            if (xTaskGetCurrentTaskHandle() != _taskHandle) {
                uint32_t waitTimeout = millis() + 1000;
                while (_taskHandle != nullptr && millis() < waitTimeout) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                if (_taskHandle != nullptr) {
                    vTaskDelete(_taskHandle);
                    _taskHandle = nullptr;
                }
            }
        }
#endif

        if (_client.connected()) {
            _client.stop();
        }
        _state = TG_IDLE;
        _clearQueue();
        return true;
    }

    // Проверка, активна ли сейчас фоновая задача FreeRTOS
    bool isTaskRunning() const {
#if defined(ESP32)
        return (_taskHandle != nullptr);
#else
        return false;
#endif
    }

    // Включен ли режим фоновой задачи в объекте
    bool isTaskMode() const {
#if defined(ESP32)
        return _useTask;
#else
        return false;
#endif
    }

    // Запущен ли бот для работы с сетью
    bool isStarted() const {
        return _started;
    }

    // Установка токена бота (тип String)
    void setBotToken(const String& token) {
        _botToken = token;
    }

    // Установка токена бота (тип const char*)
    void setBotToken(const char* token) {
        _botToken = token;
    }

    // Настройка чата по умолчанию
    void setChatID(int64_t chatId) {
        _chatId = chatId;
    }

    // Настройка топика / темы по умолчанию для супергрупп
    void setThreadID(int64_t threadId) {
        _threadId = threadId;
    }

    // Установка коллбека для входящих сообщений
    void attachCallback(CallbackType cb) {
        _callback = cb;
    }

    // Настройка Long Polling (от 10 до 50 секунд, 0 - выключено / short polling)
    void setLongPoll(uint8_t timeoutSeconds) {
        if (timeoutSeconds == 0)
            _longPollTimeout = 0;
        else
            _longPollTimeout = constrain(timeoutSeconds, 10, 50);
    }

    // Настройка периода опроса для short polling (в секундах)
    void setInterval(uint32_t intervalSeconds) {
        _pollInterval = intervalSeconds * 1000UL;
    }

    // Настройка интерактивного окна: при получении команды бот переходит в Long Polling
    // на windowSeconds секунд с момента последнего сообщения
    void setInteractiveWindow(uint32_t windowSeconds, uint8_t fastLongPollTimeout = 20) {
        _interactiveWindow = windowSeconds;
        if (fastLongPollTimeout < 10) _interactiveLongPoll = 10;
        else if (fastLongPollTimeout > 50) _interactiveLongPoll = 50;
        else _interactiveLongPoll = fastLongPollTimeout;
    }

    // Активно ли сейчас интерактивное окно
    bool isInteractive() const {
        return (_interactiveWindow > 0 && _activeUntil != 0 && (int32_t)(millis() - _activeUntil) < 0);
    }

    // Сколько секунд интерактивного окна осталось
    uint32_t getInteractiveRemaining() const {
        if (!isInteractive()) return 0;
        return (_activeUntil - millis()) / 1000UL;
    }

    // Разовая пауза всех сетевых запросов (в секундах). 0 - отмена паузы.
    void setNext(uint32_t seconds = 0) {
        if (seconds == 0) {
            _pauseUntil = 0;
            LOG_TELEGRAM_API(println, F("[TG] Pause cancelled. Resuming normal operations."));
        } else {
            _pauseUntil = millis() + (seconds * 1000UL);
            LOG_TELEGRAM_API(printf_P, PSTR("[TG] Pausing ALL requests for %u seconds.\n"), seconds);
            if (seconds > 50 || _client.connected()) {
                _client.stop();
                _state = TG_IDLE;
            }
        }
    }

    // Находится ли бот сейчас на паузе по setNext
    bool isPaused() const {
        return (_pauseUntil != 0 && (int32_t)(millis() - _pauseUntil) < 0);
    }

    // Сколько секунд паузы осталось
    uint32_t getNextRemaining() const {
        if (!isPaused()) return 0;
        return (_pauseUntil - millis()) / 1000UL;
    }

    // Готов ли бот принимать и отправлять команды
    bool isReady() const {
        return (_started && WiFi.status() == WL_CONNECTED && _state == TG_IDLE && !isPaused());
    }

    // Код последней ошибки
    int getLastError() const {
        return _lastError;
    }

    // Текстовая расшифровка кода последней ошибки
    const __FlashStringHelper* getLastErrorText() const {
        return getErrorText(_lastError);
    }

    // Статическая функция расшифровки любого кода ошибки
    static const __FlashStringHelper* getErrorText(int code) {
        switch (code) {
            case TG_OK: return F("OK");
            case TG_ERR_CONNECTION: return F("Connection failed");
            case TG_ERR_NO_WIFI: return F("WiFi not connected");
            case TG_ERR_NO_TOKEN: return F("Invalid bot token");
            case TG_ERR_TIMEOUT: return F("Response timeout");
            case TG_ERR_READ_TIMEOUT: return F("Data read timeout");
            case TG_ERR_HTTP: return F("HTTP error");
            case TG_ERR_RATE_LIMIT: return F("Rate limit (429)");
            case TG_ERR_PARSE: return F("JSON parse error");
            case TG_ERR_API: return F("Telegram API error");
            case TG_ERR_QUEUE_FULL: return F("Tx queue full");
            default: return F("Unknown error");
        }
    }

    // Количество сообщений в очереди на отправку
    size_t getQueueSize() const {
        _lockTx();
        size_t sz = _txQueue.size();
        _unlockTx();
        return sz;
    }

    // --- Методы отправки сообщений ---
    // Автоопределение HTML: если в тексте найден закрывающий тег "</", парсер автоматически включает parse_mode = "HTML"
    // Опциональный параметр onSent (callback) вызывается в момент фактической доставки с ID созданного сообщения (или 0 при ошибке)

    // Отправка сообщения в указанный чат (String) с опциональным указанием топика (threadId) и callback доставки (onSent)
    bool sendMessage(int64_t chatId, const String& text, int64_t threadId = 0, OnSentCallback onSent = nullptr) {
        return _enqueueTx(TxItem::SEND, chatId, 0, {}, text, threadId, onSent);
    }

    // Отправка сообщения в указанный чат (const char*) с опциональным указанием топика (threadId) и callback доставки (onSent)
    bool sendMessage(int64_t chatId, const char* text, int64_t threadId = 0, OnSentCallback onSent = nullptr) {
        return sendMessage(chatId, String(text), threadId, onSent);
    }

    // Отправка сообщения в указанный чат (String) с callback доставки (onSent)
    bool sendMessage(int64_t chatId, const String& text, OnSentCallback onSent) {
        return sendMessage(chatId, text, 0, onSent);
    }

    // Отправка сообщения в указанный чат (const char*) с callback доставки (onSent)
    bool sendMessage(int64_t chatId, const char* text, OnSentCallback onSent) {
        return sendMessage(chatId, String(text), 0, onSent);
    }

    // Отправка сообщения в чат по умолчанию (String) с опциональным указанием топика (threadId) и callback доставки (onSent)
    bool sendMessage(const String& text, int64_t threadId = 0, OnSentCallback onSent = nullptr) {
        return _chatId ? sendMessage(_chatId, text, threadId, onSent) : false;
    }

    // Отправка сообщения в чат по умолчанию (const char*) с опциональным указанием топика (threadId) и callback доставки (onSent)
    bool sendMessage(const char* text, int64_t threadId = 0, OnSentCallback onSent = nullptr) {
        return _chatId ? sendMessage(_chatId, String(text), threadId, onSent) : false;
    }

    // Отправка сообщения в чат по умолчанию (String) с callback доставки (onSent)
    bool sendMessage(const String& text, OnSentCallback onSent) {
        return _chatId ? sendMessage(_chatId, text, 0, onSent) : false;
    }

    // Отправка сообщения в чат по умолчанию (const char*) с callback доставки (onSent)
    bool sendMessage(const char* text, OnSentCallback onSent) {
        return _chatId ? sendMessage(_chatId, String(text), 0, onSent) : false;
    }

    // Массовая рассылка сообщения по указанному белому списку ID чатов (через запятую) с опциональным указанием топика
    bool sendMessageToAll(const String& targetWhiteList, const String& text, int64_t threadId = 0) {
        int pos1 = 0, pos2 = 0;
        pos2 = targetWhiteList.indexOf(',', pos1);
        while (pos2 >= 0) {
            String tmp = targetWhiteList.substring(pos1, pos2);
            tmp.trim();
            int64_t id = tmp.toInt();
            if (id) sendMessage(id, text, threadId);
            pos1 = pos2 + 1;
            pos2 = targetWhiteList.indexOf(',', pos1);
        }
        String tmp = targetWhiteList.substring(pos1);
        tmp.trim();
        int64_t id = tmp.toInt();
        if (id) sendMessage(id, text, threadId);
        return true;
    }

    // Массовая рассылка сообщения по белому списку объекта (whiteList) с опциональным указанием топика
    bool sendMessageToAll(const String& text, int64_t threadId = 0) {
        return whiteList.length() > 0 ? sendMessageToAll(whiteList, text, threadId) : false;
    }

    // Редактирование сообщения в указанном чате по messageId (String) с опциональным callback доставки
    bool editMessage(int64_t chatId, int64_t messageId, const String& text, OnSentCallback onSent = nullptr) {
        return _enqueueTx(TxItem::EDIT, chatId, messageId, {}, text, 0, onSent);
    }

    // Редактирование сообщения в указанном чате по messageId (const char*) с опциональным callback доставки
    bool editMessage(int64_t chatId, int64_t messageId, const char* text, OnSentCallback onSent = nullptr) {
        return editMessage(chatId, messageId, String(text), onSent);
    }

    // Редактирование сообщения в чате по умолчанию по messageId (String) с опциональным callback доставки
    bool editMessage(int64_t messageId, const String& text, OnSentCallback onSent = nullptr) {
        return _chatId ? editMessage(_chatId, messageId, text, onSent) : false;
    }

    // Редактирование сообщения в чате по умолчанию по messageId (const char*) с опциональным callback доставки
    bool editMessage(int64_t messageId, const char* text, OnSentCallback onSent = nullptr) {
        return _chatId ? editMessage(_chatId, messageId, String(text), onSent) : false;
    }

    // Удаление одного сообщения по messageId в указанном чате
    bool deleteMessage(int64_t chatId, int64_t messageId) {
        return _enqueueTx(TxItem::DELETE, chatId, messageId, {}, "", 0);
    }

    // Удаление одного сообщения по messageId в чате по умолчанию
    bool deleteMessage(int64_t messageId) {
        return _chatId ? deleteMessage(_chatId, messageId) : false;
    }

    // Групповое удаление сообщений (std::vector<int64_t>) в указанном чате
    bool deleteMessages(int64_t chatId, const std::vector<int64_t>& messageIds) {
        return _enqueueTx(TxItem::DELETE_MULTIPLE, chatId, 0, messageIds, "", 0);
    }

    // Групповое удаление сообщений через список инициализации {id1, id2, ...} в указанном чате
    bool deleteMessages(int64_t chatId, std::initializer_list<int64_t> messageIds) {
        return deleteMessages(chatId, std::vector<int64_t>(messageIds));
    }

    // Групповое удаление сообщений (std::vector<int64_t>) в чате по умолчанию
    bool deleteMessages(const std::vector<int64_t>& messageIds) {
        return _chatId ? deleteMessages(_chatId, messageIds) : false;
    }

    // Групповое удаление сообщений через список инициализации {id1, id2, ...} в чате по умолчанию
    bool deleteMessages(std::initializer_list<int64_t> messageIds) {
        return _chatId ? deleteMessages(_chatId, std::vector<int64_t>(messageIds)) : false;
    }

    // Проверка, является ли текст командой бота (начинается с command или /command)
    static bool is_command(const String& in, const String& command) {
        return TelegramCore::is_command(in, command);
    }

    // Проверка, входит ли chatId в белый список текущего объекта (whiteList)
    bool isWhitelisted(const int64_t& chatId) const {
        return TelegramCore::isWhitelisted(chatId, whiteList);
    }

    // Проверка, входит ли chatId в указанную строку белого списка (список ID через запятую)
    static bool isWhitelisted(const int64_t& chatId, const String& targetWhiteList) {
        return TelegramCore::isWhitelisted(chatId, targetWhiteList);
    }

    // Извлечение первого ID из белого списка текущего объекта (whiteList)
    int64_t extractFirstNumber() const {
        return TelegramCore::extractFirstNumber(whiteList);
    }

    // Извлечение первого ID из указанной строки со списком
    static int64_t extractFirstNumber(const String& targetWhiteList) {
        return TelegramCore::extractFirstNumber(targetWhiteList);
    }

    // Проверка новых сообщений (вызывается в loop)
    // Возвращает:
    //  > 0 : число полученных сообщений
    //  < 0 : код ошибки (TgError) при возникновении сбоя
    //    0 : штатная работа / ожидание
    int checkMessage(bool force = false) {
        if (!_started) return 0;
        if (force && !isPaused()) {
            _forcePoll = true;
        }
        tick();
        int ret = _tickStatus;
        _tickStatus = 0; // Сбрасываем разовый статус
        return ret;
    }

    // Главный неблокирующий конечный автомат (FSM)
    void tick() {
        if (!_started) return;
#if defined(ESP32)
        if (_useTask && _taskHandle != nullptr) {
            if (xTaskGetCurrentTaskHandle() != _taskHandle) {
                return; // Игнорировать внешний вызов из loop: автомат работает внутри задачи
            }
        }
#endif
        if (WiFi.status() != WL_CONNECTED) {
            if (_client.connected()) _client.stop();
            _state = TG_DISCONNECTED;
            if (_lastError != TG_ERR_NO_WIFI) {
                _setError(TG_ERR_NO_WIFI);
            }
            return;
        }

        switch (_state) {
            case TG_DISCONNECTED:
            case TG_IDLE:
                _handleIdleState();
                break;

            case TG_WAIT_DATA:
                _handleWaitDataState();
                break;

            case TG_READ_HEADERS:
                _handleReadHeadersState();
                break;

            case TG_READ_BODY:
                _handleReadBodyState();
                break;
        }
    }

private:
    enum FsmState {
        TG_DISCONNECTED,
        TG_IDLE,
        TG_WAIT_DATA,
        TG_READ_HEADERS,
        TG_READ_BODY
    };

    enum RequestType {
        REQ_NONE,
        REQ_POLL,
        REQ_TX
    };

    struct TxItem {
        enum Type { SEND, DELETE, DELETE_MULTIPLE, EDIT } type;
        int64_t chatId;
        int64_t messageId;
        std::vector<int64_t> messageIds;
        String text;
        int64_t threadId;
        OnSentCallback onSent;
    };

    TelegramClient _client;
    String _botToken;
    int64_t _chatId = 0;
    int64_t _threadId = 0;
    CallbackType _callback = nullptr;

    uint8_t _longPollTimeout = 20;   // Базовый таймаут Long Polling
    uint32_t _pollInterval = 10000; // Базовый период short polling
    uint32_t _lastPollEndTime = 0;
    int64_t _lastUpdateId = 0;

    // Интерактивное окно
    uint32_t _interactiveWindow = 0;
    uint8_t _interactiveLongPoll = 20;
    uint32_t _activeUntil = 0;
    bool _wasInteractive = false;

    FsmState _state = TG_IDLE;
    RequestType _currentReqType = REQ_NONE;
    uint8_t _currentPollTimeout = 0;
    uint32_t _requestStartTime = 0;
    uint32_t _readStartTime = 0;
    uint32_t _lastConnectAttempt = 0;
    uint32_t _reconnectBackoffUntil = 0;
    uint32_t _pauseUntil = 0;

    bool _started = false;
    int _lastError = TG_OK;
    int _tickStatus = 0;
    bool _forcePoll = false;

    // Буферы парсера HTTP
    String _headerLine;
    String _bodyBuffer;
    int _contentLength = -1;
    int _httpStatusCode = 0;
    bool _connectionClose = false;

    std::queue<TxItem> _txQueue;
    const size_t MAX_TX_QUEUE = 32;

#if defined(ESP32)
    bool _useTask = false;
    volatile bool _running = false;
    TaskHandle_t _taskHandle = nullptr;
    mutable portMUX_TYPE _txMux = portMUX_INITIALIZER_UNLOCKED;

    // Захват критической секции для очереди исходящих сообщений (ESP32)
    void _lockTx() const { portENTER_CRITICAL(&_txMux); }

    // Освобождение критической секции для очереди исходящих сообщений (ESP32)
    void _unlockTx() const { portEXIT_CRITICAL(&_txMux); }

    // Рабочая функция задачи FreeRTOS на ESP32
    static void _taskWorker(void* param) {
        AsyncTelegramAPI* self = static_cast<AsyncTelegramAPI*>(param);
        while (self->_running) {
            self->tick();

            if (self->_state == TG_IDLE && self->_isQueueEmpty()) {
                vTaskDelay(pdMS_TO_TICKS(10));
            } else {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
        self->_taskHandle = nullptr;
        vTaskDelete(NULL);
    }
#else
    // Заглушка мьютекса для одноядерного ESP8266
    void _lockTx() const {}

    // Заглушка мьютекса для одноядерного ESP8266
    void _unlockTx() const {}
#endif

    // Проверка, пуста ли очередь исходящих сообщений
    bool _isQueueEmpty() const {
        _lockTx();
        bool empty = _txQueue.empty();
        _unlockTx();
        return empty;
    }

    // Очистка очереди исходящих сообщений и уведомление callback о сбросе
    void _clearQueue() {
        _lockTx();
        while (!_txQueue.empty()) {
            TxItem item = _txQueue.front();
            _txQueue.pop();
            if (item.onSent) {
                item.onSent(0);
            }
        }
        _unlockTx();
    }

    // Сохранение кода последней ошибки и статуса текущего тика
    void _setError(TgError err) {
        _lastError = err;
        _tickStatus = err;
    }

    // Добавление запроса в очередь исходящих пакетов (отправка, правка, удаление)
    bool _enqueueTx(TxItem::Type type, int64_t chatId, int64_t messageId, 
                    const std::vector<int64_t>& ids, const String& text, 
                    int64_t threadId, OnSentCallback onSent = nullptr) {
        if (!_started) {
            LOG_TELEGRAM_API(println, F("[TG] Bot is stopped! Message dropped."));
            if (onSent) onSent(0);
            return false;
        }
        _lockTx();
        if (_txQueue.size() >= MAX_TX_QUEUE) {
            _unlockTx();
            LOG_TELEGRAM_API(println, F("[TG] Tx queue full! Dropping item."));
            _setError(TG_ERR_QUEUE_FULL);
            if (onSent) onSent(0);
            return false;
        }
        _txQueue.push({type, chatId, messageId, ids, text, threadId, onSent});
        _unlockTx();
        return true;
    }

    // Проверка и установка безопасного TLS соединения с api.telegram.org:443
    bool _ensureConnected() {
        if (_client.connected()) return true;

        if (millis() < _reconnectBackoffUntil) return false;
        if (millis() - _lastConnectAttempt < 3000) return false;

        _lastConnectAttempt = millis();
        LOG_TELEGRAM_API(println, F("[TG] Connecting to api.telegram.org:443..."));

        bool ok = _client.connect("api.telegram.org", 443);
        if (!ok) {
            LOG_TELEGRAM_API(println, F("[TG] Connection failed!"));
            _setError(TG_ERR_CONNECTION);
            return false;
        }
        LOG_TELEGRAM_API(printf_P, PSTR("[TG] TLS connected! Free heap: %u bytes\n"), ESP.getFreeHeap());
        return true;
    }

    // Обработка состояния ожидания (TG_IDLE): отправка очереди или запуск опроса getUpdates
    void _handleIdleState() {
        if (isPaused()) {
            return;
        }
        _pauseUntil = 0;

        // 1. Приоритет: отправка исходящих сообщений
        if (!_isQueueEmpty()) {
            if (!_ensureConnected()) return;
            _sendTxRequest();
            return;
        }

        // 2. Опрос входящих сообщений
        if (_botToken.length() < 30) {
            _setError(TG_ERR_NO_TOKEN);
            return;
        }

        bool activeWindow = isInteractive();
        if (_wasInteractive && !activeWindow) {
            _wasInteractive = false;
            LOG_TELEGRAM_API(println, F("[TG] Interactive window expired. Returning to base polling mode."));
        } else if (activeWindow) {
            _wasInteractive = true;
        }

        uint8_t effectiveLongPoll = activeWindow ? _interactiveLongPoll : _longPollTimeout;

        bool readyToPoll = false;
        if (_forcePoll) {
            readyToPoll = true;
            _forcePoll = false;
        } else if (effectiveLongPoll > 0) {
            if (millis() - _lastPollEndTime >= 10) {
                readyToPoll = true;
            }
        } else if (_pollInterval > 0) {
            if (_lastPollEndTime == 0 || millis() - _lastPollEndTime >= _pollInterval) {
                readyToPoll = true;
            }
        }

        if (readyToPoll) {
            if (!_ensureConnected()) {
                if (_pollInterval > 3000) {
                    _lastPollEndTime = millis() - _pollInterval + 3000;
                }
                return;
            }
            _sendPollRequest(effectiveLongPoll);
        }
    }

    // Извлечение следующего элемента из очереди и запуск HTTP POST запроса
    void _sendTxRequest() {
        TxItem item;
        _lockTx();
        if (!_txQueue.empty()) {
            item = _txQueue.front();
        } else {
            _unlockTx();
            return;
        }
        _unlockTx();

        String payload;
        const __FlashStringHelper* method = nullptr;

        if (item.type == TxItem::SEND) {
            method = F("sendMessage");
            int64_t tid = (item.threadId > 0) ? item.threadId : _threadId;
            payload = TelegramCore::buildSendMessagePayload(item.chatId, item.text.c_str(), tid);
        } else if (item.type == TxItem::EDIT) {
            method = F("editMessageText");
            payload = TelegramCore::buildEditMessagePayload(item.chatId, item.messageId, item.text.c_str());
        } else if (item.type == TxItem::DELETE) {
            method = F("deleteMessage");
            payload = TelegramCore::buildDeleteMessagePayload(item.chatId, item.messageId);
        } else if (item.type == TxItem::DELETE_MULTIPLE) {
            method = F("deleteMessages");
            payload = TelegramCore::buildDeleteMessagesPayload(item.chatId, item.messageIds);
        }

        LOG_TELEGRAM_API(print, F("[TG] -> Sending "));
        LOG_TELEGRAM_API(print, method);
        LOG_TELEGRAM_API(printf_P, PSTR(" to chat %lld (len %d)\n"), 
                         (long long)item.chatId, (int)payload.length());

        _sendHttpRequest(method, payload);
        _currentReqType = REQ_TX;
        _requestStartTime = millis();
        _state = TG_WAIT_DATA;
    }

    // Формирование и отправка запроса getUpdates (Long Polling / Short Polling)
    void _sendPollRequest(uint8_t longPollTimeout) {
        String payload = TelegramCore::buildGetUpdatesPayload(_lastUpdateId + 1, 5, longPollTimeout);
        LOG_TELEGRAM_API(printf_P, PSTR("[TG] -> Sending getUpdates (offset: %lld, timeout: %d)\n"), 
                         (long long)(_lastUpdateId + 1), (int)longPollTimeout);
        _currentPollTimeout = longPollTimeout;
        _sendHttpRequest(F("getUpdates"), payload);
        _currentReqType = REQ_POLL;
        _requestStartTime = millis();
        _state = TG_WAIT_DATA;
    }

    // Отправка сформированного HTTP POST пакета на сервер Telegram
    void _sendHttpRequest(const __FlashStringHelper* method, const String& payload) {
        bool keepAlive = (_currentReqType == REQ_POLL) ? (_currentPollTimeout > 0) : (_longPollTimeout > 0 || isInteractive());

        String req;
        req.reserve(256 + payload.length());
        req += F("POST /bot");
        req += _botToken;
        req += F("/");
        req += method;
        req += F(" HTTP/1.1\r\nHost: api.telegram.org\r\nContent-Type: application/json\r\n");

        if (keepAlive) {
            req += F("Connection: keep-alive\r\n");
        } else {
            req += F("Connection: close\r\n");
        }

        req += F("Content-Length: ");
        req += String(payload.length());
        req += F("\r\n\r\n");
        req += payload;

        _client.print(req);
        _client.flush();

        _headerLine = "";
        _bodyBuffer = "";
        _contentLength = -1;
        _httpStatusCode = 0;
        _connectionClose = false;
    }

    // Ожидание ответа от сервера Telegram (WAIT_DATA) с контролем таймаутов
    void _handleWaitDataState() {
        if (_currentReqType == REQ_POLL && !_isQueueEmpty()) {
            LOG_TELEGRAM_API(println, F("[TG] Tx queued during poll. Aborting poll for urgent Tx."));
            _client.stop();
            _state = TG_IDLE;
            return;
        }

        if (!_client.connected() && !_client.available()) {
            LOG_TELEGRAM_API(println, F("[TG] Connection dropped by server while waiting for response."));
            _client.stop();
            _state = TG_IDLE;
            if (_currentReqType == REQ_POLL) {
                _lastPollEndTime = millis();
            }
            _setError(TG_ERR_CONNECTION);
            _notifyTxFailed();
            return;
        }

        uint32_t waitTimeout = (_currentReqType == REQ_POLL && _currentPollTimeout > 0)
                               ? ((_currentPollTimeout + 10) * 1000UL)
                               : 10000UL;

        if (millis() - _requestStartTime > waitTimeout) {
            LOG_TELEGRAM_API(println, F("[TG] Request timeout! Closing socket."));
            _client.stop();
            _state = TG_IDLE;
            if (_currentReqType == REQ_POLL) {
                _lastPollEndTime = millis();
            }
            _setError(TG_ERR_TIMEOUT);
            _notifyTxFailed();
            return;
        }

        if (_client.available() == 0) {
            return;
        }

        _readStartTime = millis();
        _state = TG_READ_HEADERS;
        _handleReadHeadersState();
    }

    // Построчное чтение и парсинг HTTP-заголовков ответа сервера (READ_HEADERS)
    void _handleReadHeadersState() {
        while (_client.available()) {
            char c = (char)_client.read();
            _headerLine += c;
            if (c == '\n') {
                if (_headerLine.startsWith(F("HTTP/1."))) {
                    int spaceIdx = _headerLine.indexOf(' ');
                    if (spaceIdx > 0) {
                        _httpStatusCode = _headerLine.substring(spaceIdx + 1, spaceIdx + 4).toInt();
                    }
                } else if (_headerLine.startsWith(F("Content-Length:")) || _headerLine.startsWith(F("content-length:"))) {
                    int colonIdx = _headerLine.indexOf(':');
                    _contentLength = _headerLine.substring(colonIdx + 1).toInt();
                } else if (_headerLine.startsWith(F("Connection: close")) || _headerLine.startsWith(F("connection: close"))) {
                    _connectionClose = true;
                } else if (_headerLine == F("\r\n") || _headerLine == F("\n")) {
                    _headerLine = "";
                    _bodyBuffer = "";
                    if (_contentLength > 0) {
                        _bodyBuffer.reserve(_contentLength + 1);
                    }
                    _state = TG_READ_BODY;
                    _handleReadBodyState();
                    return;
                }
                _headerLine = "";
            }
        }

        if (millis() - _readStartTime > 10000UL) {
            LOG_TELEGRAM_API(println, F("[TG] Headers read timeout!"));
            _client.stop();
            _state = TG_IDLE;
            if (_currentReqType == REQ_POLL) {
                _lastPollEndTime = millis();
            }
            _setError(TG_ERR_READ_TIMEOUT);
            _notifyTxFailed();
        }
    }

    // Потоковое чтение тела ответа HTTP (READ_BODY) до Content-Length
    void _handleReadBodyState() {
        while (_client.available() && (_contentLength < 0 || (int)_bodyBuffer.length() < _contentLength)) {
            _bodyBuffer += (char)_client.read();
        }

        bool complete = false;
        if (_contentLength >= 0) {
            complete = ((int)_bodyBuffer.length() >= _contentLength);
        } else {
            complete = !_client.connected();
        }

        if (!complete) {
            if (millis() - _readStartTime > 10000UL) {
                LOG_TELEGRAM_API(println, F("[TG] Body read timeout!"));
                _client.stop();
                _state = TG_IDLE;
                if (_currentReqType == REQ_POLL) {
                    _lastPollEndTime = millis();
                }
                _setError(TG_ERR_READ_TIMEOUT);
                _notifyTxFailed();
                return;
            }
            return;
        }

        _parseResponseBody();

        // Закрываем сокет, если следующий режим - Short Polling, или если сервер запросил закрытие
        bool nextIsLongPoll = isInteractive() || (_longPollTimeout > 0);
        if (!nextIsLongPoll || _connectionClose || !_client.connected()) {
            _client.stop();
        }

        _state = TG_IDLE;
        if (_currentReqType == REQ_POLL) {
            _lastPollEndTime = millis();
        }
    }

    // Парсинг JSON-тела ответа сервера и диспетчеризация результатов
    void _parseResponseBody() {
        LOG_TELEGRAM_API(printf_P, PSTR("[TG] <- HTTP %d, Content-Length: %d\n"), _httpStatusCode, _contentLength);

        if (_httpStatusCode == 200) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, _bodyBuffer);
            if (!err && doc[F("ok")].as<bool>()) {
                _lastError = TG_OK;
                if (_currentReqType == REQ_POLL) {
                    _processUpdates(doc);
                } else if (_currentReqType == REQ_TX) {
                    LOG_TELEGRAM_API(println, F("[TG] Tx sent successfully!"));
                    int64_t newMsgId = 0;
                    JsonObject resObj = doc[F("result")];
                    if (resObj && resObj[F("message_id")]) {
                        newMsgId = resObj[F("message_id")].as<int64_t>();
                        LOG_TELEGRAM_API(printf_P, PSTR("[TG] Sent message ID: %lld\n"), (long long)newMsgId);
                    }
                    OnSentCallback cb = nullptr;
                    _lockTx();
                    if (!_txQueue.empty()) {
                        TxItem item = _txQueue.front();
                        _txQueue.pop();
                        cb = item.onSent;
                    }
                    _unlockTx();
                    if (cb) {
                        cb(newMsgId);
                    }
                }
            } else if (err) {
                LOG_TELEGRAM_API(printf_P, PSTR("[TG] JSON parse error: %s\n"), err.c_str());
                _setError(TG_ERR_PARSE);
                _notifyTxFailed();
            } else {
#if defined(DEBUG) || defined(TG_DEBUG)
                int errCode = doc[F("error_code")] | 0;
                const char* desc = doc[F("description")] | "API error";
                LOG_TELEGRAM_API(printf_P, PSTR("[TG] API error %d: %s\n"), errCode, desc);
#endif
                _setError(TG_ERR_API);
                _notifyTxFailed();
            }
        } else if (_httpStatusCode == 429) {
            JsonDocument errDoc;
            deserializeJson(errDoc, _bodyBuffer);
            int retryAfter = errDoc[F("parameters")][F("retry_after")] | 5;
            _reconnectBackoffUntil = millis() + (retryAfter * 1000UL);
            LOG_TELEGRAM_API(printf_P, PSTR("[TG] Rate limit 429! Backoff for %d seconds\n"), retryAfter);
            _setError(TG_ERR_RATE_LIMIT);
            _notifyTxFailed();
        } else {
            LOG_TELEGRAM_API(printf_P, PSTR("[TG] HTTP error: %d, body: %s\n"), _httpStatusCode, _bodyBuffer.c_str());
            _setError(TG_ERR_HTTP);
            _notifyTxFailed();
        }
    }

    // Уведомление callback о сбое доставки сообщения при ошибке соединения или API
    void _notifyTxFailed() {
        OnSentCallback cb = nullptr;
        _lockTx();
        if (_currentReqType == REQ_TX && !_txQueue.empty()) {
            TxItem item = _txQueue.front();
            _txQueue.pop();
            cb = item.onSent;
        }
        _unlockTx();
        if (cb) {
            cb(0); // 0 = ошибка доставки
        }
    }

    // Разбор массива updates ответа getUpdates, фильтрация и вызов пользовательского callback
    void _processUpdates(JsonDocument& doc) {
        int receivedCount = 0;
        JsonArray updates = doc[F("result")].as<JsonArray>();
        for (JsonObject update : updates) {
            int64_t updateId = update[F("update_id")].as<int64_t>();
            if (updateId >= _lastUpdateId) {
                _lastUpdateId = updateId;
            }

            JsonObject message = update[F("message")];
            if (message) {
                int64_t chatId = message[F("chat")][F("id")].as<int64_t>();

                // Проверка строгого режима (strict):
                // Если strict == true, сообщение разрешается только если оно пришло от чата по умолчанию (_chatId)
                // или входит в белый список (whiteList). Если strict == false, принимаются сообщения от всех.
                if (strict) {
                    bool allowed = (_chatId != 0 && chatId == _chatId);
                    if (!allowed && whiteList.length() > 0) {
                        allowed = TelegramCore::isWhitelisted(chatId, whiteList);
                    }
                    if (!allowed) {
                        continue;
                    }
                }

                TResult res;
                res.chatId = chatId;
                res.messageId = message[F("message_id")].as<int64_t>();
                res.threadId = message[F("message_thread_id")] | 0;
                res.text = message[F("text")].as<String>();
                res.from = message[F("from")][F("first_name")].as<String>();

                LOG_TELEGRAM_API(printf_P, PSTR("[TG] Update %lld from %s (Chat %lld, Thread %lld): %s\n"),
                                 (long long)updateId, res.from.c_str(), 
                                 (long long)chatId, (long long)res.threadId, res.text.c_str());

                receivedCount++;

                // Продление активного интерактивного окна
                if (_interactiveWindow > 0) {
                    _activeUntil = millis() + (_interactiveWindow * 1000UL);
                    LOG_TELEGRAM_API(printf_P, PSTR("[TG] Interactive window extended for %u sec.\n"), _interactiveWindow);
                }

                if (_callback) {
                    String reply = _callback(res);
                    if (reply.length() > 0) {
                        // Ответ отправляется в тот же тред, откуда пришла команда
                        sendMessage(chatId, reply, res.threadId);
                    }
                }
            }
        }
        _tickStatus += receivedCount;
    }
};

#ifdef LOG_TELEGRAM_API
#undef LOG_TELEGRAM_API
#endif

#endif // ASYNC_TELEGRAM_API_H
