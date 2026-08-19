#include <Arduino.h>

// Set true any time you want to go back to raw byte inspection
const bool DEBUG_HEXDUMP = false;

const int UWB_PACKET_SIZE = 37; // confirmed from your hex dump
const int NUM_SLOTS = 8;        // slot index == tag's configured Device ID (0-7)

const int UWB_RX_PIN = 16;
const int UWB_TX_PIN = 17;

float tagDistance[NUM_SLOTS] = {0};
bool  tagSeen[NUM_SLOTS] = {false};

unsigned long lastPrintTime = 0;

// -------------------------------------------------
// Distance for a given tag ID/slot (0-7)
// -------------------------------------------------
float getDistance(uint8_t *packet, int tagId)
{
    int index = 3 + (tagId * 4); // same 4-bytes-per-slot pattern as your tag-side code
    uint16_t distanceMM = packet[index] | (packet[index + 1] << 8);
    return distanceMM / 1000.0;
}

void setup()
{
    Serial.begin(115200);
    Serial1.begin(115200, SERIAL_8N1, UWB_RX_PIN, UWB_TX_PIN);

    Serial.println();
    Serial.println("ESP32 UWB Base Station Reader Started");
}

void loop()
{
    if (DEBUG_HEXDUMP) {
        static unsigned long lastByteTime = 0;
        static int col = 0;
        while (Serial1.available()) {
            if (millis() - lastByteTime > 100 && col != 0) { Serial.println(); col = 0; }
            uint8_t b = Serial1.read();
            if (b < 0x10) Serial.print('0');
            Serial.print(b, HEX);
            Serial.print(' ');
            lastByteTime = millis();
            if (++col >= 16) { Serial.println(); col = 0; }
        }
        return;
    }

    static uint8_t packet[UWB_PACKET_SIZE];
    static int packetIndex = 0;
    static bool receivingPacket = false;

    while (Serial1.available())
    {
        uint8_t b = Serial1.read();

        if (!receivingPacket)
        {
            if (b == 0xAA)
            {
                receivingPacket = true;
                packetIndex = 0;
                packet[packetIndex++] = b;
            }
        }
        else
        {
            packet[packetIndex++] = b;

            if (packetIndex >= UWB_PACKET_SIZE)
            {
                receivingPacket = false;
                packetIndex = 0;

                if (packet[0] == 0xAA && packet[1] == 0x25 && packet[2] == 0x01)
                {
                    for (int id = 0; id < NUM_SLOTS; id++)
                    {
                        float d = getDistance(packet, id);
                        if (d > 0.0)
                        {
                            tagDistance[id] = d;
                            tagSeen[id] = true;
                        }
                    }
                }
            }
        }
    }

    if (millis() - lastPrintTime >= 1000)
    {
        lastPrintTime = millis();

        Serial.print("BS1 -> ");
        bool any = false;
        for (int id = 0; id < NUM_SLOTS; id++)
        {
            if (tagSeen[id])
            {
                Serial.printf("Tag(ID%d): %.3f m   ", id, tagDistance[id]);
                any = true;
            }
        }
        if (!any) Serial.print("no tags detected yet");
        Serial.println();
    }
}
