#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <painlessMesh.h>
#include <WiFi.h>
#include <esp_system.h>
#include <Preferences.h>

// =========================================================
// ESP04 - TRACKER / PACKET SOURCE
// MAC: 88:F1:55:13:07:50
// =========================================================


// =========================================================
// MESH SETTINGS
// =========================================================

#define MESH_PREFIX   "MiningMesh"
#define MESH_PASSWORD "MineMesh2026"
#define MESH_PORT     5555

Scheduler userScheduler;
painlessMesh mesh;

const char *DEVICE_NAME = "ESP04";
const char *EXPECTED_MAC = "88:F1:55:13:07:50";


// =========================================================
// PACKET TIMING
// =========================================================

const unsigned long SEND_INTERVAL = 5000;   // Send every 5 seconds

unsigned long lastSendTime = 0;

uint32_t packetSequence = 0;
uint32_t bootID = 0;


// =========================================================
// REAL TIME CLOCK
// =========================================================

RTC_DS1307 rtc;

// Used to remember whether this firmware version
// has already set the RTC.
Preferences preferences;


// =========================================================
// UWB MODULE
// =========================================================

#define UWB_RX_PIN 16
#define UWB_TX_PIN 17
#define UWB_BAUD   115200

HardwareSerial UwbSerial(2);


// =========================================================
// BU03 FRAME
// =========================================================

#define FRAME_LEN 37


// =========================================================
// LATEST UWB MEASUREMENTS
// =========================================================

// Base Station 1
uint32_t latestDist1_mm = 0;

// Base Station 2
uint32_t latestDist2_mm = 0;

// Base Station 3
uint32_t latestDist3_mm = 0;

// Time the latest UWB measurement was received
uint32_t latestMeasurementUnix = 0;

// Becomes true once we receive a valid UWB frame
bool haveValidMeasurement = false;


// =========================================================
// READ LITTLE-ENDIAN uint32_t
// =========================================================

uint32_t readUint32LE(uint8_t *buf, int offset)
{
  return (uint32_t)buf[offset]
       | ((uint32_t)buf[offset + 1] << 8)
       | ((uint32_t)buf[offset + 2] << 16)
       | ((uint32_t)buf[offset + 3] << 24);
}


// =========================================================
// CONVERT RTC TIME TO TEXT
// =========================================================

String formatDateTime(DateTime dt)
{
  char buffer[25];

  snprintf(
    buffer,
    sizeof(buffer),
    "%04d-%02d-%02d_%02d:%02d:%02d",
    dt.year(),
    dt.month(),
    dt.day(),
    dt.hour(),
    dt.minute(),
    dt.second()
  );

  return String(buffer);
}


// =========================================================
// INITIALISE RTC
// =========================================================
//
// The compile date/time changes whenever you upload
// a newly compiled program.
//
// ESP32 Preferences remembers the build timestamp.
//
// This means:
//
// NEW upload -> RTC gets set
//
// Normal reset / power cycle -> RTC keeps running normally
//
// =========================================================

void initialiseRTC()
{
  Serial.println();
  Serial.println("Initialising RTC...");

  if (!rtc.begin())
  {
    Serial.println("ERROR: Couldn't find DS1307 RTC!");

    while (1)
    {
      delay(10);
    }
  }


  // Create a unique ID for this firmware build
  String currentBuild =
    String(__DATE__) + "_" + String(__TIME__);


  // Open ESP32 non-volatile memory
  preferences.begin("rtcinit", false);


  // Read the build that last set the RTC
  String previousBuild =
    preferences.getString("build", "");


  // -------------------------------------------------------
  // If this is a new firmware build, set the RTC
  // -------------------------------------------------------

  if (previousBuild != currentBuild)
  {
    Serial.println("New firmware detected.");
    Serial.println("Setting RTC to compile/upload time...");

    rtc.adjust(
      DateTime(
        F(__DATE__),
        F(__TIME__)
      )
    );


    // Remember that this build has set the RTC
    preferences.putString(
      "build",
      currentBuild
    );

    Serial.println("RTC time has been set.");
  }

  // -------------------------------------------------------
  // RTC exists but oscillator is not running
  // -------------------------------------------------------

  else if (!rtc.isrunning())
  {
    Serial.println("RTC was not running.");
    Serial.println("Resetting RTC time...");

    rtc.adjust(
      DateTime(
        F(__DATE__),
        F(__TIME__)
      )
    );
  }

  else
  {
    Serial.println("RTC already running.");
    Serial.println("Keeping existing RTC time.");
  }


  preferences.end();


  // Display current RTC time
  DateTime now = rtc.now();

  Serial.print("RTC time: ");
  Serial.println(formatDateTime(now));
}


// =========================================================
// MESH CONNECTION CALLBACKS
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
// SEND TRACKER PACKET
// =========================================================

void sendTrackerPacket()
{
  packetSequence++;


  // Current RTC time when packet is sent
  DateTime sendTime = rtc.now();


  // Time of actual UWB measurement
  String measurementTime;


  if (haveValidMeasurement)
  {
    DateTime measurementDT(
      latestMeasurementUnix
    );

    measurementTime =
      formatDateTime(measurementDT);
  }

  else
  {
    measurementTime = "NO_DATA";
  }


  String mac = WiFi.macAddress();


  // =======================================================
  // BUILD PACKET
  //
  // Example:
  //
  // PKT
  // |SRC=ESP04
  // |MAC=88:F1:55:13:07:50
  // |BOOT=123456
  // |SEQ=12
  // |TIME=2026-09-02_16:30:00
  // |B1_MM=3210
  // |B2_MM=4570
  // |B3_MM=2890
  // |VALID=1
  //
  // =======================================================

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


  // ---------- BASE STATION 1 ----------

  packet += "|B1_MM=";

  if (haveValidMeasurement)
  {
    packet += String(latestDist1_mm);
  }
  else
  {
    packet += "-1";
  }


  // ---------- BASE STATION 2 ----------

  packet += "|B2_MM=";

  if (haveValidMeasurement)
  {
    packet += String(latestDist2_mm);
  }
  else
  {
    packet += "-1";
  }


  // ---------- BASE STATION 3 ----------

  packet += "|B3_MM=";

  if (haveValidMeasurement)
  {
    packet += String(latestDist3_mm);
  }
  else
  {
    packet += "-1";
  }


  // ---------- VALID FLAG ----------

  packet += "|VALID=";

  packet += haveValidMeasurement
          ? "1"
          : "0";


  // =======================================================
  // SEND INTO MESH
  // =======================================================

  bool success =
    mesh.sendBroadcast(packet);


  // =======================================================
  // SERIAL MONITOR OUTPUT
  // =======================================================

  Serial.println();
  Serial.println(
    "========================================"
  );

  Serial.print("SENDING PACKET #");
  Serial.println(packetSequence);


  Serial.print("Source: ");
  Serial.println(DEVICE_NAME);


  Serial.print("MAC: ");
  Serial.println(mac);


  Serial.print("Measurement Time: ");
  Serial.println(measurementTime);


  if (haveValidMeasurement)
  {
    Serial.println();

    Serial.print("Base 1: ");
    Serial.print(
      latestDist1_mm / 1000.0,
      3
    );
    Serial.println(" m");


    Serial.print("Base 2: ");
    Serial.print(
      latestDist2_mm / 1000.0,
      3
    );
    Serial.println(" m");


    Serial.print("Base 3: ");
    Serial.print(
      latestDist3_mm / 1000.0,
      3
    );
    Serial.println(" m");
  }

  else
  {
    Serial.println(
      "No UWB measurement available yet."
    );
  }


  Serial.println();

  Serial.print("Mesh send: ");

  if (success)
  {
    Serial.println("SUCCESS");
  }

  else
  {
    Serial.println("FAILED");
  }


  Serial.println();
  Serial.println("RAW PACKET:");

  Serial.println(packet);


  Serial.println(
    "========================================"
  );
}


// =========================================================
// SETUP
// =========================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);


  Serial.println();
  Serial.println(
    "========================================"
  );

  Serial.println(
    "ESP04 TRACKER STARTING"
  );

  Serial.println(
    "========================================"
  );


  // =======================================================
  // RTC
  // =======================================================

  initialiseRTC();


  // =======================================================
  // UWB UART
  // =======================================================

  UwbSerial.begin(
    UWB_BAUD,
    SERIAL_8N1,
    UWB_RX_PIN,
    UWB_TX_PIN
  );


  Serial.println();
  Serial.println(
    "UWB UART started."
  );


  // =======================================================
  // MESH
  // =======================================================

  mesh.setDebugMsgTypes(
    ERROR | STARTUP
  );


  mesh.init(
    MESH_PREFIX,
    MESH_PASSWORD,
    &userScheduler,
    MESH_PORT
  );


  mesh.onReceive(
    &receivedCallback
  );


  mesh.onNewConnection(
    &newConnectionCallback
  );


  mesh.onChangedConnections(
    &changedConnectionCallback
  );


  // =======================================================
  // BOOT ID
  // =======================================================

  // Random ID generated every time ESP04 starts.
  //
  // BOOT + SEQ means each packet can be uniquely
  // identified.

  bootID = esp_random();


  // =======================================================
  // DEVICE INFORMATION
  // =======================================================

  String actualMAC =
    WiFi.macAddress();


  Serial.println();

  Serial.print("ESP name: ");
  Serial.println(DEVICE_NAME);


  Serial.print("WiFi MAC: ");
  Serial.println(actualMAC);


  Serial.print("Mesh Node ID: ");
  Serial.println(
    mesh.getNodeId()
  );


  Serial.print("Boot ID: ");
  Serial.println(bootID);


  // Check we uploaded code to the correct ESP
  if (
    !actualMAC.equalsIgnoreCase(
      EXPECTED_MAC
    )
  )
  {
    Serial.println();

    Serial.println("WARNING:");

    Serial.println(
      "MAC does not match expected ESP04 MAC."
    );
  }


  Serial.println();

  Serial.println(
    "Waiting for BU03 ranging data..."
  );
}


// =========================================================
// LOOP
// =========================================================

void loop()
{
  // =======================================================
  // PAINLESS MESH
  // =======================================================
  //
  // VERY IMPORTANT:
  // This must run continuously.
  //
  // =======================================================

  mesh.update();


  // =======================================================
  // READ BU03 RANGING FRAME
  // =======================================================

  static uint8_t buf[FRAME_LEN];

  static int len = 0;


  while (
    UwbSerial.available()
  )
  {
    uint8_t b =
      UwbSerial.read();


    // -----------------------------------------------------
    // Look for start byte
    // -----------------------------------------------------

    if (len == 0)
    {
      if (b == 0xAA)
      {
        buf[len++] = b;
      }
    }


    // -----------------------------------------------------
    // Continue collecting frame
    // -----------------------------------------------------

    else
    {
      buf[len++] = b;


      // ===================================================
      // COMPLETE FRAME RECEIVED
      // ===================================================

      if (len == FRAME_LEN)
      {
        // Check end byte
        if (
          buf[FRAME_LEN - 1]
          == 0x55
        )
        {
          // ===============================================
          // READ THREE BASE STATION DISTANCES
          // ===============================================
          //
          // Base 1:
          // bytes 3 - 6
          //
          // Base 2:
          // bytes 7 - 10
          //
          // Base 3:
          // bytes 11 - 14
          //
          // ===============================================


          latestDist1_mm =
            readUint32LE(
              buf,
              3
            );


          latestDist2_mm =
            readUint32LE(
              buf,
              7
            );


          latestDist3_mm =
            readUint32LE(
              buf,
              11
            );


          // ===============================================
          // RECORD MEASUREMENT TIME
          // ===============================================

          DateTime measurementTime =
            rtc.now();


          latestMeasurementUnix =
            measurementTime.unixtime();


          haveValidMeasurement = true;


          // ===============================================
          // LIVE UWB SERIAL DISPLAY
          // ===============================================

          Serial.print(
            "UWB -> Base1: "
          );

          Serial.print(
            latestDist1_mm / 1000.0,
            3
          );


          Serial.print(
            " m | Base2: "
          );

          Serial.print(
            latestDist2_mm / 1000.0,
            3
          );


          Serial.print(
            " m | Base3: "
          );

          Serial.print(
            latestDist3_mm / 1000.0,
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


        // Ready to receive next frame
        len = 0;
      }
    }
  }


  // =======================================================
  // SEND LATEST DATA EVERY 5 SECONDS
  // =======================================================

  if (
    millis() - lastSendTime
    >= SEND_INTERVAL
  )
  {
    lastSendTime =
      millis();


    sendTrackerPacket();
  }
}