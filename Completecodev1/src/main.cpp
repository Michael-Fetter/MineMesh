/*
  Mesh Collector + Live Tracker Map Server
  --------------------------------------------------------------------
  This ESP32 does two jobs at once:

    1. MESH COLLECTOR: joins the painlessMesh network and listens for
       tracker packets (PKT|SRC=...|B1_MM=...|B2_MM=...|B3_MM=...|VALID=1)
       broadcast by the tag nodes. The three distances are parsed out
       and stored in global variables.

    2. WEB SERVER: serves your index.html / three.js / .glb map files
       from LittleFS, and exposes a /data endpoint that returns the
       *current* global distance values as JSON, polled by the page's
       own trilaterate() function.

  IMPORTANT - NO SEPARATE WIFI ACCESS POINT:
    painlessMesh already creates its own AP (named MESH_PREFIX below).
    An ESP32 can only run one AP at a time, so this sketch does NOT
    also call WiFi.softAP() with a different network - that would
    conflict with the mesh. Instead:

      - Connect your laptop's WiFi to the network named by MESH_PREFIX
        (see below), using MESH_PASSWORD.
      - Then open a browser to http://192.168.4.1/ - that's this
        node's mesh AP address, where the web server is listening.

  REQUIRED ONE-TIME SETUP (same as before):
    1. index.html, three/, and models/ must be inside a "data" folder
       in this PlatformIO project's root (next to platformio.ini).
    2. platformio.ini under [env:esp32dev] needs:
         board_build.partitions = no_ota.csv
         board_build.filesystem = littlefs
         lib_deps =
           painlessMesh
           bblanchon/ArduinoJson
    3. Upload the filesystem separately via PlatformIO's
       "Upload Filesystem Image" task (or "pio run --target uploadfs"),
       in addition to uploading this sketch.
*/

#include <Arduino.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <painlessMesh.h>

// =========================================================
// MESH SETTINGS - must match the tracker/tag ESP32s exactly
// =========================================================
#define MESH_PREFIX   "MiningMesh"
#define MESH_PASSWORD "MineMesh2026"
#define MESH_PORT     5555

Scheduler userScheduler;
painlessMesh mesh;

WebServer server(80);

// =========================================================
// LATEST TRACKER DATA - shared between the mesh callback and
// the web server's /data handler. Both run cooperatively from
// the same loop() (mesh.update() then server.handleClient()),
// so no locking is needed.
// =========================================================
volatile long   latestBase1_mm = 0;
volatile long   latestBase2_mm = 0;
volatile long   latestBase3_mm = 0;
volatile bool   latestValid = false;
String          latestSource = "";
unsigned long   lastPacketMillis = 0;

// =========================================================
// Extract a value from a packet like:
// PKT|SRC=ESP04|MAC=...|BOOT=...|SEQ=...|TIME=...|B1_MM=3200|B2_MM=4500|B3_MM=2800|VALID=1
// =========================================================
String getPacketValue(const String &packet, const String &key) {
  String searchText = "|" + key + "=";
  int start = packet.indexOf(searchText);
  if (start == -1) return "";
  start += searchText.length();
  int end = packet.indexOf('|', start);
  if (end == -1) end = packet.length();
  return packet.substring(start, end);
}

// =========================================================
// MESH RECEIVE CALLBACK - parses tracker packets and updates
// the global distance variables used by the web server
// =========================================================
void receivedCallback(uint32_t from, String &msg) {
  Serial.println();
  Serial.print("Mesh packet from node ");
  Serial.println(from);

  if (!msg.startsWith("PKT")) {
    Serial.print("Unknown mesh message: ");
    Serial.println(msg);
    return;
  }

  String source          = getPacketValue(msg, "SRC");
  String base1Text       = getPacketValue(msg, "B1_MM");
  String base2Text       = getPacketValue(msg, "B2_MM");
  String base3Text       = getPacketValue(msg, "B3_MM");
  String validText       = getPacketValue(msg, "VALID");
  String measurementTime = getPacketValue(msg, "TIME");

  bool isValid = (validText == "1");

  if (isValid) {
    latestBase1_mm = base1Text.toInt();
    latestBase2_mm = base2Text.toInt();
    latestBase3_mm = base3Text.toInt();
    latestValid = true;
    latestSource = source;
    lastPacketMillis = millis();

    Serial.print("Source: "); Serial.println(source);
    Serial.print("Measurement Time: "); Serial.println(measurementTime);
    Serial.print("Base1: "); Serial.print(latestBase1_mm / 1000.0, 3); Serial.println(" m");
    Serial.print("Base2: "); Serial.print(latestBase2_mm / 1000.0, 3); Serial.println(" m");
    Serial.print("Base3: "); Serial.print(latestBase3_mm / 1000.0, 3); Serial.println(" m");
  } else {
    latestValid = false;
    Serial.println("Received packet marked invalid - not updating distances.");
  }
}

void newConnectionCallback(uint32_t nodeId) {
  Serial.print("New mesh connection: ");
  Serial.println(nodeId);
}

void changedConnectionCallback() {
  Serial.println("Mesh connections changed.");
}

// =========================================================
// FILE SERVING (LittleFS) - unchanged from the original sketch
// =========================================================
String getContentType(const String& path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".js"))   return "application/javascript";
  if (path.endsWith(".css"))  return "text/css";
  if (path.endsWith(".glb"))  return "model/gltf-binary";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  return "application/octet-stream";
}

bool serveFile(String path) {
  if (path.endsWith("/")) path += "index.html";
  if (!LittleFS.exists(path)) return false;
  File file = LittleFS.open(path, "r");
  server.streamFile(file, getContentType(path));
  file.close();
  return true;
}

void handleFileRequest() {
  String path = server.uri();
  if (!serveFile(path)) {
    server.send(404, "text/plain", "File not found: " + path);
  }
}

// =========================================================
// /data endpoint - now serves the REAL, live mesh-sourced
// distances instead of placeholder/uninitialized values
// =========================================================
void handleDataJson() {

  float d1 = latestBase1_mm / 1000.0;
  float d2 = latestBase2_mm / 1000.0;
  float d3 = latestBase3_mm / 1000.0;

  String json = "{";
  json += "\"d1\":" + String(d1, 3) + ",";
  json += "\"d2\":" + String(d2, 3) + ",";
  json += "\"d3\":" + String(d3, 3) + ",";
  json += "\"valid\":" + String(latestValid ? "true" : "false") + ",";
  json += "\"source\":\"" + latestSource + "\",";
  json += "\"ageMs\":" + String(millis() - lastPacketMillis);
  json += "}";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("========================================");
  Serial.println("MESH COLLECTOR + MAP SERVER STARTING");
  Serial.println("========================================");

  // --- Mount filesystem containing the map files ---
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed! Did you run 'Upload Filesystem Image'?");
  } else {
    Serial.println("LittleFS mounted successfully");
  }

  // --- Start the mesh (this also creates the AP the laptop connects to) ---
  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);

  Serial.print("Collector Mesh Node ID: ");
  Serial.println(mesh.getNodeId());
  Serial.print("Connect your laptop's WiFi to: ");
  Serial.println(MESH_PREFIX);
  Serial.println(WiFi.localIP());

  // --- Web server routes ---
  server.on("/data", HTTP_GET, handleDataJson);
  server.onNotFound(handleFileRequest); // serves index.html, three/*, models/*

  server.begin();
  Serial.println("HTTP server started");
  Serial.println("Waiting for tracker packets...");
}

void loop() {
  mesh.update();       // must run continuously for painlessMesh
  server.handleClient();
}