#ifndef TELEGRAM_CORE_H
#define TELEGRAM_CORE_H

#include <Arduino.h>
#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>
#include <vector>

struct TResult {
    int64_t chatId;
    int64_t messageId;
    int64_t threadId; // ID топика/темы в супергруппе (если есть, иначе 0)
    String text;
    String from;
};

class TelegramCore {
public:
    // Поиск команды с учетом / или без
    static bool is_command(const String& in, const String& command) {
        int pos = in.indexOf(command);
        return pos == 0 || pos == 1;
    }

    // Проверка, есть ли чат в белом списке
    static bool isWhitelisted(const int64_t& chatId, const String& whiteList) {
        return (chatId && whiteList.length() > 0 && whiteList.indexOf(String(chatId)) >= 0);
    }

    // Извлечение первого ID из списка
    static int64_t extractFirstNumber(String whiteList) {
        whiteList.trim();
        return whiteList.toInt();
    }

    // Формирование JSON для getUpdates
    static String buildGetUpdatesPayload(int64_t offset, uint8_t limit = 5, uint8_t timeout = 0) {
        JsonDocument doc;
        doc[F("offset")] = offset;
        doc[F("limit")] = limit;
        if (timeout > 0) {
            doc[F("timeout")] = timeout;
        }
        String payload;
        serializeJson(doc, payload);
        return payload;
    }

    // Формирование JSON для sendMessage (с поддержкой меню [MENU], HTML-тегов и топиков)
    static String buildSendMessagePayload(int64_t chatId, const char* message, int64_t threadId = 0) {
        JsonDocument doc;
        doc[F("chat_id")] = chatId;

        if (threadId > 0) {
            doc[F("message_thread_id")] = threadId;
        }

        // Автоопределение HTML-разметки: если в тексте найден закрывающий тег "</"
        if (strstr_P(message, PSTR("</"))) {
            doc[F("parse_mode")] = F("HTML");
        }

        const char* token = strstr_P(message, PSTR("[MENU]"));

        if (token) {
            char text[strlen(message) + 1];
            strcpy(text, message);
            char* source = text;

            char* mtoken = strstr_P(source, PSTR("[MENU]"));
            *mtoken = 0;
            doc[F("text")] = source;

            JsonObject reply_markup = doc[F("reply_markup")].to<JsonObject>();
            source = mtoken + 6; // пропустить [MENU]

            if (*source) {
                reply_markup[F("resize_keyboard")] = true;
                reply_markup[F("persistent")] = true;
                JsonArray reply_markup_keyboard = reply_markup[F("keyboard")].to<JsonArray>();

                const char* delim = "\n\t";
                uint8_t row = 0, col = 0;
                bool new_row = false;
                char* ptoken = strpbrk(source, delim);

                while (ptoken) {
                    if (*ptoken == '\n') new_row = true;
                    *ptoken = 0;
                    reply_markup_keyboard[row][col][F("text")] = source;
                    source = ptoken + 1;
                    if (new_row) {
                        col = 0; row++; new_row = false;
                    } else {
                        col++;
                    }
                    ptoken = strpbrk(source, delim);
                }
                reply_markup_keyboard[row][col][F("text")] = source;
            } else {
                reply_markup[F("remove_keyboard")] = true;
            }
        } else {
            doc[F("text")] = message;
        }

        String payload;
        serializeJson(doc, payload);
        return payload;
    }

    // Формирование JSON для editMessageText
    static String buildEditMessagePayload(int64_t chatId, int64_t messageId, const char* message) {
        JsonDocument doc;
        doc[F("chat_id")] = chatId;
        doc[F("message_id")] = messageId;
        doc[F("text")] = message;

        // Автоопределение HTML-разметки
        if (strstr_P(message, PSTR("</"))) {
            doc[F("parse_mode")] = F("HTML");
        }

        String payload;
        serializeJson(doc, payload);
        return payload;
    }

    // Формирование JSON для deleteMessage
    static String buildDeleteMessagePayload(int64_t chatId, int64_t messageId) {
        JsonDocument doc;
        doc[F("chat_id")] = chatId;
        doc[F("message_id")] = messageId;
        String payload;
        serializeJson(doc, payload);
        return payload;
    }

    // Формирование JSON для deleteMessages
    static String buildDeleteMessagesPayload(int64_t chatId, const std::vector<int64_t>& messageIds) {
        JsonDocument doc;
        doc[F("chat_id")] = chatId;
        JsonArray array = doc[F("message_ids")].to<JsonArray>();
        for (auto id : messageIds) {
            array.add(id);
        }
        String payload;
        serializeJson(doc, payload);
        return payload;
    }
};

#endif
