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
#define CHAT_ID   0 // Укажите ваш chat_id или оставьте 0 для приема от любых

AsyncTelegramAPI bot;

String handleTelegram(TResult& msg) {
    Serial.printf("[TG] Message from %s (Chat %lld): %s\n", 
                  msg.from.c_str(), (long long)msg.chatId, msg.text.c_str());

    if (bot.is_command(msg.text, F("hello"))) {
        return "Hello, " + msg.from + "!";
    }
    if (bot.is_command(msg.text, F("menu"))) {
        return "Main Menu:[MENU]hello\tstatus\nhelp\thide";
    }
    if (bot.is_command(msg.text, F("hide"))) {
        return "Menu hidden[MENU]";
    }
    if (bot.is_command(msg.text, F("status"))) {
        return "<b>Uptime:</b> " + String(millis() / 1000) + " s\n"
               "<b>Free RAM:</b> " + String(ESP.getFreeHeap()) + " B";
    }

    return "Echo: " + msg.text;
}

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");

    bot.setBotToken(BOT_TOKEN);
    if (CHAT_ID != 0) bot.setChatID(CHAT_ID);
    bot.attachCallback(handleTelegram);

    // Short polling каждые 60 секунд (сокет закрывается в покое)
    bot.setLongPoll(0);
    bot.setInterval(60);

    // Интерактивное ускорение: при получении команды включается Long Polling 20 сек на 3 минуты
    bot.setInteractiveWindow(180, 20);

    bot.start();
    Serial.println("Bot started!");
}

void loop() {
    // Неблокирующий автомат (< 1 мкс в покое)
    bot.tick();

    // Ваш основной неблокирующий код
}
