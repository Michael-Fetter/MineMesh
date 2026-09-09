#include <Arduino.h>

// ESP32 UART pins connected to UWB
#define UWB_RX 16
#define UWB_TX 17

// Use UART2
HardwareSerial UWBSerial(2);

void sendATCommand(String command, int waitTime)
{
    // Remove anything left in the receive buffer
    while (UWBSerial.available())
    {
        UWBSerial.read();
    }

    Serial.print("Sending: ");
    Serial.println(command);

    // Send command to UWB
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
    // USB connection between ESP32 and VS Code
    Serial.begin(115200);

    delay(2000);

    Serial.println();
    Serial.println("Starting UWB configuration...");

    // UART connection between ESP32 and UWB
    UWBSerial.begin(
        115200,
        SERIAL_8N1,
        UWB_RX,
        UWB_TX
    );

    delay(2000);

    // Configure UWB
    sendATCommand("AT+SETCFG=0,0,1,1", 1000);

    // Save configuration
    sendATCommand("AT+SAVE", 3000);

    // Read configuration back
    sendATCommand("AT+GETCFG", 1000);

    Serial.println("UWB configuration finished.");
}


void loop()
{
}