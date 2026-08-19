#include <Arduino.h>

HardwareSerial UWB(2);

void sendCommand(const char* command)
{
    Serial.print("Sending: ");
    Serial.println(command);

    UWB.print(command);
    UWB.print("\r\n");

    delay(1000);

    while (UWB.available()) {
        Serial.write(UWB.read());
    }

    Serial.println();
}

void setup()
{
    Serial.begin(115200);
    UWB.begin(115200, SERIAL_8N1, 16, 17);

    delay(2000);

    sendCommand("AT");
    sendCommand("AT+SETCFG=0,0,1,1");
    sendCommand("AT+SAVE");
    sendCommand("AT+GETCFG");
}

void loop()
{
}