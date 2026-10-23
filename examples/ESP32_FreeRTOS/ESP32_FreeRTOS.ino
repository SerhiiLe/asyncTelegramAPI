#include <WiFi.h>
#include <AsyncTelegramAPI.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* pass = "YOUR_WIFI_PASSWORD";
#define BOT_TOKEN "YOUR_BOT_TOKEN"
#define CHAT_ID   0

// true = запуск в отдельной фоновой задаче FreeRTOS на ESP32
AsyncTelegramAPI bot(true);

String handleTelegram(TResult& msg) {
    if (bot.is_command(msg.text, F("ping"))) {
        return "pong!";
    }
    if (bot.is_command(msg.text, F("uptime"))) {
        return "Uptime: " + String(millis() / 1000) + " s";
    }
    return "Received: " + msg.text;
}

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    Serial.println("WiFi Connected!");

    bot.setBotToken(BOT_TOKEN);
    if (CHAT_ID != 0) bot.setChatID(CHAT_ID);
    bot.attachCallback(handleTelegram);

    // Постоянный Long Polling 25 секунд
    bot.setLongPoll(25);

    // Запуск бота и фоновой задачи (core=1, stackSize=8192, priority=1)
    bot.start();
    Serial.println("Bot running in background FreeRTOS task!");
}

void loop() {
    // loop() свободен на 100%! bot.tick() вызывать не нужно.
    delay(1000);
}
