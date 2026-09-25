#include <Arduino.h>

#define UWB_RX 16
#define UWB_TX 17
#define UWB_BAUD 115200

HardwareSerial UWBSerial(2);

void sendATCommand(const String& command, unsigned long waitTime)
{
    while (UWBSerial.available())
    {
        UWBSerial.read();
    }

    Serial.print("Sending: ");
    Serial.println(command);

    UWBSerial.print(command);
    UWBSerial.print("\r\n");

    delay(waitTime);

    Serial.println("Response:");

    while (UWBSerial.available())
    {
        Serial.write(UWBSerial.read());
    }

    Serial.println();
    Serial.println("---------------------");
}

void setup()
{
    Serial.begin(115200);

    delay(2000);

    Serial.println();
    Serial.println("Starting UWB configuration...");

    UWBSerial.begin(
        UWB_BAUD,
        SERIAL_8N1,
        UWB_RX,
        UWB_TX
    );

    delay(2000);

    // AT+SETCFG=ID,MODE,CHANNEL,RATE
    // ID:      0-7
    // MODE:    0 = Tag, 1 = Base
    // CHANNEL: 0 or 1
    // RATE:    0 = 850 kbps, 1 = 6.8 Mbps

    sendATCommand("AT+SETCFG=4,0,1,1", 1000);
    sendATCommand("AT+SAVE", 3000);
    sendATCommand("AT+GETCFG", 1000);

    Serial.println("UWB configuration finished.");
}

void loop()
{
}