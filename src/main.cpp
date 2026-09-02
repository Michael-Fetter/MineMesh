#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <painlessMesh.h>
#include <WiFi.h>
#include <esp_system.h>

// =========================================================
// ESP04 - TRACKER / PACKET SOURCE
// MAC: 88:F1:55:13:07:50
// =========================================================

// ---------- Mesh ----------
#define MESH_PREFIX   "MiningMesh"
#define MESH_PASSWORD "MineMesh2026"
#define MESH_PORT     5555

Scheduler userScheduler;
painlessMesh mesh;

const char *DEVICE_NAME = "ESP04";
const char *EXPECTED_MAC = "88:F1:55:13:07:50";

// ---------- Packet timing ----------
const unsigned long SEND_INTERVAL = 5000;   // 5 seconds
unsigned long lastSendTime = 0;

uint32_t packetSequence = 0;
uint32_t bootID = 0;

// ---------- RTC ----------
RTC_DS1307 rtc;

// ---------- UWB module ----------
#define UWB_RX_PIN 16
#define UWB_TX_PIN 17
#define UWB_BAUD   115200

HardwareSerial UwbSerial(2);

// ---------- BU03 frame ----------
#define FRAME_LEN 37

// Latest UWB measurement
uint32_t latestDist1_mm = 0;
uint32_t latestDist2_mm = 0;
uint32_t latestMeasurementUnix = 0;

bool haveValidMeasurement = false;


// =========================================================
// Read little-endian uint32
// =========================================================

uint32_t readUint32LE(uint8_t *buf, int offset)
{
  return (uint32_t)buf[offset]
       | ((uint32_t)buf[offset + 1] << 8)
       | ((uint32_t)buf[offset + 2] << 16)
       | ((uint32_t)buf[offset + 3] << 24);
}


// =========================================================
// Convert RTC time to text
// =========================================================

String formatDateTime(DateTime dt)
{
  char buffer[25];

  snprintf(buffer,
           sizeof(buffer),
           "%04d-%02d-%02d_%02d:%02d:%02d",
           dt.year(),
           dt.month(),
           dt.day(),
           dt.hour(),
           dt.minute(),
           dt.second());

  return String(buffer);
}


// =========================================================
// Mesh connection callbacks
// =========================================================

void receivedCallback(uint32_t from, String &msg)
{
  Serial.print("Mesh packet received from node ");
  Serial.print(from);
  Serial.print(": ");
  Serial.println(msg);
}


void newConnectionCallback(uint32_t nodeId)
{
  Serial.print("NEW MESH CONNECTION: ");
  Serial.println(nodeId);
}


void changedConnectionCallback()
{
  Serial.println("Mesh connections changed.");
}


// =========================================================
// Send tracker packet
// =========================================================

void sendTrackerPacket()
{
  packetSequence++;

  DateTime sendTime = rtc.now();

  String measurementTime;

  if (haveValidMeasurement)
  {
    DateTime measurementDT(latestMeasurementUnix);
    measurementTime = formatDateTime(measurementDT);
  }
  else
  {
    measurementTime = "NO_DATA";
  }

  String mac = WiFi.macAddress();

  // -------------------------------------------------------
  // Packet format:
  //
  // PKT
  // SRC
  // MAC
  // BOOT
  // SEQ
  // TIME
  // B1_MM
  // B2_MM
  // VALID
  // -------------------------------------------------------

  String packet = "PKT";

  packet += "|SRC=";
  packet += DEVICE_NAME;

  packet += "|MAC=";
  packet += mac;

  packet += "|BOOT=";
  packet += String(bootID);

  packet += "|SEQ=";
  packet += String(packetSequence);

  packet += "|TIME=";
  packet += measurementTime;

  packet += "|B1_MM=";

  if (haveValidMeasurement)
    packet += String(latestDist1_mm);
  else
    packet += "-1";

  packet += "|B2_MM=";

  if (haveValidMeasurement)
    packet += String(latestDist2_mm);
  else
    packet += "-1";

  packet += "|VALID=";
  packet += haveValidMeasurement ? "1" : "0";


  // ---------- Send into mesh ----------
  bool success = mesh.sendBroadcast(packet);


  // ---------- Serial Monitor ----------
  Serial.println();
  Serial.println("========================================");
  Serial.print("SENDING PACKET #");
  Serial.println(packetSequence);

  Serial.print("Source: ");
  Serial.println(DEVICE_NAME);

  Serial.print("MAC: ");
  Serial.println(mac);

  Serial.print("Time: ");
  Serial.println(measurementTime);

  if (haveValidMeasurement)
  {
    Serial.print("Base1: ");
    Serial.print(latestDist1_mm / 1000.0, 3);
    Serial.println(" m");

    Serial.print("Base2: ");
    Serial.print(latestDist2_mm / 1000.0, 3);
    Serial.println(" m");
  }
  else
  {
    Serial.println("No UWB measurement available yet.");
  }

  Serial.print("Mesh send: ");

  if (success)
    Serial.println("SUCCESS");
  else
    Serial.println("FAILED");

  Serial.println();
  Serial.println("RAW PACKET:");
  Serial.println(packet);

  Serial.println("========================================");
}


// =========================================================
// SETUP
// =========================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println("ESP04 TRACKER STARTING");
  Serial.println("========================================");


  // ---------- RTC ----------
  if (!rtc.begin())
  {
    Serial.println("Couldn't find RTC!");
    while (1)
    {
      delay(10);
    }
  }

  if (!rtc.isrunning())
  {
    Serial.println("RTC not running - setting compile time.");

    rtc.adjust(
      DateTime(
        F(__DATE__),
        F(__TIME__)
      )
    );
  }


  // ---------- UWB ----------
  UwbSerial.begin(
    UWB_BAUD,
    SERIAL_8N1,
    UWB_RX_PIN,
    UWB_TX_PIN
  );


  // ---------- Mesh ----------
  mesh.setDebugMsgTypes(ERROR | STARTUP);

  mesh.init(
    MESH_PREFIX,
    MESH_PASSWORD,
    &userScheduler,
    MESH_PORT
  );

  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);


  // Random number generated each time ESP04 boots.
  // Combined with SEQ this makes packets uniquely identifiable.
  bootID = esp_random();


  // ---------- Device information ----------
  String actualMAC = WiFi.macAddress();

  Serial.print("ESP name: ");
  Serial.println(DEVICE_NAME);

  Serial.print("WiFi MAC: ");
  Serial.println(actualMAC);

  Serial.print("Mesh Node ID: ");
  Serial.println(mesh.getNodeId());

  Serial.print("Boot ID: ");
  Serial.println(bootID);

  if (!actualMAC.equalsIgnoreCase(EXPECTED_MAC))
  {
    Serial.println();
    Serial.println("WARNING:");
    Serial.println("MAC does not match expected ESP04 MAC.");
  }

  Serial.println();
  Serial.println("Waiting for BU03 ranging data...");
}


// =========================================================
// LOOP
// =========================================================

void loop()
{
  // VERY IMPORTANT:
  // painlessMesh needs this running constantly.
  mesh.update();


  // =======================================================
  // Read BU03 ranging frames
  // =======================================================

  static uint8_t buf[FRAME_LEN];

  static int len = 0;


  while (UwbSerial.available())
  {
    uint8_t b = UwbSerial.read();


    if (len == 0)
    {
      if (b == 0xAA)
      {
        buf[len++] = b;
      }
    }

    else
    {
      buf[len++] = b;


      if (len == FRAME_LEN)
      {
        if (buf[FRAME_LEN - 1] == 0x55)
        {
          // ---------- Distances ----------
          latestDist1_mm = readUint32LE(buf, 3);
          latestDist2_mm = readUint32LE(buf, 7);


          // Record exactly when this measurement arrived
          DateTime measurementTime = rtc.now();

          latestMeasurementUnix =
            measurementTime.unixtime();

          haveValidMeasurement = true;


          // Optional live UWB display
          Serial.print("UWB -> Base1: ");

          Serial.print(
            latestDist1_mm / 1000.0,
            3
          );

          Serial.print(" m | Base2: ");

          Serial.print(
            latestDist2_mm / 1000.0,
            3
          );

          Serial.println(" m");
        }

        else
        {
          Serial.println(
            "Frame end marker mismatch."
          );
        }


        len = 0;
      }
    }
  }


  // =======================================================
  // Every 5 seconds package latest reading and send
  // =======================================================

  if (millis() - lastSendTime >= SEND_INTERVAL)
  {
    lastSendTime = millis();

    sendTrackerPacket();
  }
}