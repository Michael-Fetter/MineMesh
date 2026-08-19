// ReadingDataBS1,2,3
//this isnt going to work //
#include <Arduino.h>

const int UWB_PACKET_SIZE = 35;

// Latest distances
float distance1 = 0.0;
float distance2 = 0.0;
float distance3 = 0.0;

// Timing
unsigned long lastPrintTime = 0;

// -------------------------------------------------
// READ DISTANCE FROM UWB PACKET
// -------------------------------------------------

float getDistance(uint8_t *packet, int baseNumber)
{
    int index = 3 + (baseNumber * 4);

    uint16_t distanceMM =
        packet[index] |
        (packet[index + 1] << 8);

    // Convert millimetres to metres
    return distanceMM / 1000.0;
}

// -------------------------------------------------
// SETUP
// -------------------------------------------------

void setup()
{
    // USB connection to Serial Monitor
    Serial.begin(115200);

    // UWB tag connection
    Serial1.begin(115200);

    Serial.println();
    Serial.println("UWB Distance Reader Started");
    Serial.println("Waiting for tag data...");
}

// -------------------------------------------------
// MAIN LOOP
// -------------------------------------------------

void loop()
{
    static uint8_t packet[UWB_PACKET_SIZE];
    static int packetIndex = 0;
    static bool receivingPacket = false;

    // Read data coming from UWB tag
    while (Serial1.available())
    {
        uint8_t incomingByte = Serial1.read();

        // Look for start of packet
        if (!receivingPacket)
        {
            if (incomingByte == 0xAA)
            {
                receivingPacket = true;
                packetIndex = 0;
                packet[packetIndex++] = incomingByte;
            }
        }
        else
        {
            packet[packetIndex++] = incomingByte;

            // Full 35-byte packet received
            if (packetIndex >= UWB_PACKET_SIZE)
            {
                receivingPacket = false;
                packetIndex = 0;

                // Check packet header
                if (packet[0] == 0xAA &&
                    packet[1] == 0x25 &&
                    packet[2] == 0x01)
                {
                    // Update latest distances
                    distance3 = getDistance(packet, 3);
                    distance1 = getDistance(packet, 1);
                    distance2 = getDistance(packet, 2);
                }
            }
        }
    }

    // Print latest distances once every second
    if (millis() - lastPrintTime >= 1000)
    {
        lastPrintTime = millis();

        Serial.print("BS3: ");
        Serial.print(distance3, 3);
        Serial.print(" m    ");

        Serial.print("BS1: ");
        Serial.print(distance1, 3);
        Serial.print(" m    ");

        Serial.print("BS2: ");
        Serial.print(distance2, 3);
        Serial.println(" m");
    }
}