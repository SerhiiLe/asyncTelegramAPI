#include <Arduino.h>
#if defined(ESP8266)
    #include <ESP8266WiFi.h>
#elif defined(ESP32)
    #include <WiFi.h>
#endif
#include <AsyncTelegramAPI.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* pass = "YOUR_WIFI_PASSWORD";
#define BOT_TOKEN "YOUR_BOT_TOKEN"
#define CHAT_ID   123456789 // Замените на ваш chat_id

AsyncTelegramAPI bot;
int64_t dashboardMessageId = 0;
int counter = 0;

String handleTelegram(TResult& msg) {
    if (bot.is_command(msg.text, F("reset"))) {
        counter = 0;
        return "Counter reset!";
    }
    return "";
}

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) delay(500);

    bot.setBotToken(BOT_TOKEN);
    bot.setChatID(CHAT_ID);
    bot.attachCallback(handleTelegram);
    bot.setLongPoll(20);
    bot.start();

    // Отправляем начальное сообщение и сохраняем его ID для последующего редактирования
    bot.sendMessage(CHAT_ID, "<b>Dashboard Starting...</b>", [](int64_t msgId) {
        dashboardMessageId = msgId;
        Serial.printf("Dashboard message ID: %lld\n", (long long)msgId);
    });
}

void loop() {
    bot.tick();

    // Обновляем дашборд каждые 30 секунд через editMessage без спама новыми сообщениями
    static uint32_t lastUpdate = 0;
    if (dashboardMessageId != 0 && millis() - lastUpdate > 30000UL) {
        lastUpdate = millis();
        counter++;

        String text = "<b>Sensor Dashboard:</b>\n"
                      "<code>"
                      "Uptime : " + String(millis() / 1000) + " s\n"
                      "Count  : " + String(counter) + "\n"
                      "RAM    : " + String(ESP.getFreeHeap()) + " B\n"
                      "Queue  : " + String(bot.getQueueSize()) +
                      "</code>";

        bot.editMessage(CHAT_ID, dashboardMessageId, text);
    }
}
