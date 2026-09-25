#include <Arduino.h>
#include <painlessMesh.h>
#include <WiFi.h>
#include <esp_system.h>

const char* TAG_ID = "TAG01";

#define MESH_PREFIX   "MiningMesh"
#define MESH_PASSWORD "MineMesh2026"
#define MESH_PORT     5555

#define UWB_RX_PIN 16
#define UWB_TX_PIN 17
#define UWB_BAUD   115200

Scheduler scheduler;
painlessMesh mesh;
HardwareSerial UwbSerial(2);

const int FRAME_LENGTH = 37;
const unsigned long SEND_INTERVAL = 100;

uint32_t base1_mm = 0;
uint32_t base2_mm = 0;
uint32_t base3_mm = 0;

bool haveMeasurement = false;

unsigned long measurementTimeMs = 0;
unsigned long lastSendTime = 0;

uint32_t bootID = 0;
uint32_t sequence = 0;

uint32_t readUint32LE(const uint8_t* data, int start)
{
    return (uint32_t)data[start]
         | ((uint32_t)data[start + 1] << 8)
         | ((uint32_t)data[start + 2] << 16)
         | ((uint32_t)data[start + 3] << 24);
}

uint32_t cleanDistance(uint32_t distance)
{
    if (distance == 0 || distance == 0xFFFFFFFF)
    {
        return 0;
    }

    return distance;
}

void readUWB()
{
    static uint8_t frame[FRAME_LENGTH];
    static int index = 0;

    while (UwbSerial.available())
    {
        uint8_t incomingByte = UwbSerial.read();

        if (index == 0 && incomingByte != 0xAA)
        {
            continue;
        }

        frame[index++] = incomingByte;

        if (index == FRAME_LENGTH)
        {
            bool validFrame =
                frame[0] == 0xAA &&
                frame[1] == 0x25 &&
                frame[FRAME_LENGTH - 1] == 0x55;

            if (validFrame)
            {
                base1_mm = cleanDistance(
                    readUint32LE(frame, 7)
                );

                base2_mm = cleanDistance(
                    readUint32LE(frame, 11)
                );

                base3_mm = cleanDistance(
                    readUint32LE(frame, 15)
                );

                haveMeasurement =
                    base1_mm > 0 ||
                    base2_mm > 0 ||
                    base3_mm > 0;

                if (haveMeasurement)
                {
                    measurementTimeMs = millis();
                }

                Serial.print(TAG_ID);

                Serial.print(" | B1: ");
                Serial.print(base1_mm);
                Serial.print(" mm");

                Serial.print(" | B2: ");
                Serial.print(base2_mm);
                Serial.print(" mm");

                Serial.print(" | B3: ");
                Serial.print(base3_mm);
                Serial.println(" mm");
            }
            else
            {
                Serial.println("Invalid UWB frame.");
            }

            index = 0;
        }
    }
}

void sendPacket()
{
    sequence++;

    String packet = "TAG";

    packet += "|ID=";
    packet += TAG_ID;

    packet += "|MAC=";
    packet += WiFi.macAddress();

    packet += "|BOOT=";
    packet += String(bootID);

    packet += "|SEQ=";
    packet += String(sequence);

    packet += "|TIME_MS=";

    if (haveMeasurement)
    {
        packet += String(measurementTimeMs);
    }
    else
    {
        packet += "NO_DATA";
    }

    packet += "|B1_MM=";
    packet += String(base1_mm);

    packet += "|B2_MM=";
    packet += String(base2_mm);

    packet += "|B3_MM=";
    packet += String(base3_mm);

    packet += "|VALID=";
    packet += haveMeasurement ? "1" : "0";

    bool success = mesh.sendBroadcast(packet);

    Serial.println();
    Serial.print("Sending: ");
    Serial.println(packet);

    Serial.print("Mesh: ");
    Serial.println(
        success ? "SUCCESS" : "FAILED"
    );
}

void setup()
{
    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("======================");
    Serial.println("UWB TAG STARTING");
    Serial.println("======================");

    Serial.print("Tag ID: ");
    Serial.println(TAG_ID);

    UwbSerial.begin(
        UWB_BAUD,
        SERIAL_8N1,
        UWB_RX_PIN,
        UWB_TX_PIN
    );

    Serial.println("UWB UART started.");

    mesh.setDebugMsgTypes(
        ERROR | STARTUP
    );

    mesh.init(
        MESH_PREFIX,
        MESH_PASSWORD,
        &scheduler,
        MESH_PORT
    );

    Serial.println("Mesh started.");

    bootID = esp_random();

    Serial.print("ESP32 MAC: ");
    Serial.println(
        WiFi.macAddress()
    );

    Serial.print("Mesh Node ID: ");
    Serial.println(
        mesh.getNodeId()
    );

    Serial.print("Boot ID: ");
    Serial.println(
        bootID
    );

    Serial.println();
    Serial.println(
        "Waiting for UWB measurements..."
    );
}

void loop()
{
    mesh.update();

    readUWB();

    if (millis() - lastSendTime >= SEND_INTERVAL)
    {
        lastSendTime = millis();

        sendPacket();
    }
}