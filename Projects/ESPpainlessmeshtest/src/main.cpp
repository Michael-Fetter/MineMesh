#include <Arduino.h>
#include <WiFi.h>
#include <painlessMesh.h>

/*
    ============================================================
    FOUR ESP32 PAINLESSMESH TEST
    ============================================================

    Upload this exact same program to all four ESP32 boards.

    Board identification:

    ESP1 = 8C:94:DF:A0:DE:40
    ESP2 = 88:F1:55:13:07:14
    ESP3 = 88:F1:55:13:06:A8
    ESP4 = 88:F1:55:13:07:50

    Each ESP:

    1. Reads its own Wi-Fi MAC address.
    2. Identifies itself as ESP1, ESP2, ESP3 or ESP4.
    3. Joins the painlessMesh network.
    4. Uses painlessMesh's synchronised relative clock.
    5. Produces fake UWB distance data.
    6. Broadcasts that data to the other ESPs.
    7. Prints received messages in Serial Monitor.

    No external RTC is required for this test.

    mesh.getNodeTime() gives synchronised relative mesh time,
    not real date and time.
*/


// ============================================================
// MESH NETWORK SETTINGS
// These must be identical on all four ESP32s.
// ============================================================

#define MESH_PREFIX   "UWB_TRACKING_MESH"
#define MESH_PASSWORD "UWBmesh315"
#define MESH_PORT     5555


// ============================================================
// SYNCHRONISED TRANSMISSION TIMING
// ============================================================

/*
    The mesh is divided into repeating five-second frames.

    ESP1 transmits near 0.25 seconds into each frame.
    ESP2 transmits near 1.25 seconds into each frame.
    ESP3 transmits near 2.25 seconds into each frame.
    ESP4 transmits near 3.25 seconds into each frame.

    This spreads the transmissions apart and demonstrates that
    all boards are using the same mesh timebase.
*/

constexpr uint32_t MESH_FRAME_US = 5000000UL;
constexpr uint32_t SLOT_SPACING_US = 1000000UL;
constexpr uint32_t FIRST_SLOT_OFFSET_US = 250000UL;
constexpr uint32_t SLOT_WINDOW_US = 250000UL;


// ============================================================
// PAINLESSMESH OBJECTS
// ============================================================

Scheduler userScheduler;
painlessMesh mesh;


// ============================================================
// INFORMATION ABOUT THIS PHYSICAL ESP32
// ============================================================

String thisNodeName = "UNKNOWN";
String thisNodeMac = "UNKNOWN";

/*
    ESP1 = node number 1
    ESP2 = node number 2
    ESP3 = node number 3
    ESP4 = node number 4

    Unknown boards use number 5.
*/

uint8_t thisNodeNumber = 5;


// ============================================================
// PACKET AND TIME INFORMATION
// ============================================================

uint32_t packetSequence = 0;

/*
    Stores the most recent five-second frame in which this ESP
    transmitted.

    Starting at 0xFFFFFFFF means no frame has been sent yet.
*/

uint32_t lastSentFrame = 0xFFFFFFFFUL;

/*
    Number of painlessMesh clock adjustments experienced by
    this ESP since startup.
*/

uint32_t timeAdjustmentCount = 0;

/*
    Most recent clock adjustment applied by painlessMesh.

    Positive means the ESP clock was moved forward.
    Negative means the ESP clock was moved backward.
*/

int32_t lastTimeAdjustmentUs = 0;


// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

void identifyThisESP();
void sendSamplePacket();
void checkTransmissionSlot();

void receivedCallback(uint32_t from, String &message);
void newConnectionCallback(uint32_t nodeId);
void changedConnectionCallback();
void nodeTimeAdjustedCallback(int32_t offset);


// ============================================================
// IDENTIFY THIS ESP32 USING ITS MAC ADDRESS
// ============================================================

void identifyThisESP()
{
    /*
        The MAC addresses you previously recorded were station
        interface MAC addresses.

        WiFi.mode(WIFI_STA) enables that interface before reading
        its MAC address.
    */

    WiFi.mode(WIFI_STA);
    delay(200);

    thisNodeMac = WiFi.macAddress();
    thisNodeMac.toUpperCase();


    if (thisNodeMac == "8C:94:DF:A0:DE:40")
    {
        thisNodeName = "ESP1";
        thisNodeNumber = 1;
    }
    else if (thisNodeMac == "88:F1:55:13:07:14")
    {
        thisNodeName = "ESP2";
        thisNodeNumber = 2;
    }
    else if (thisNodeMac == "88:F1:55:13:06:A8")
    {
        thisNodeName = "ESP3";
        thisNodeNumber = 3;
    }
    else if (thisNodeMac == "88:F1:55:13:07:50")
    {
        thisNodeName = "ESP4";
        thisNodeNumber = 4;
    }
    else
    {
        /*
            The board can still join the mesh, but it will appear
            as UNKNOWN in the transmitted packets.
        */

        thisNodeName = "UNKNOWN";
        thisNodeNumber = 5;
    }
}


// ============================================================
// CREATE AND TRANSMIT FAKE UWB DATA
// ============================================================

void sendSamplePacket()
{
    packetSequence++;


    /*
        Capture the timestamp at the moment the fake measurement
        is created.

        Later, this is where we will timestamp the real BU-03
        measurement.
    */

    uint32_t measurementMeshTimeUs = mesh.getNodeTime();


    /*
        Generate a different fake distance for each ESP.

        Approximate ranges:

        ESP1: 1.75 metres
        ESP2: 2.50 metres
        ESP3: 3.25 metres
        ESP4: 4.00 metres

        The value changes slightly with every packet so you can
        see that new measurements are arriving.
    */

    float fakeDistanceMetres =
        1.00f +
        (0.75f * static_cast<float>(thisNodeNumber)) +
        (0.01f * static_cast<float>(packetSequence % 20));


    /*
        millis() is the local uptime for this ESP only.

        It is not synchronised with the other boards.
    */

    uint32_t localUptimeMs = millis();


    /*
        Count every other node that this ESP currently knows
        about, including indirectly connected mesh nodes.
    */

    uint32_t knownRemoteNodes =
        static_cast<uint32_t>(mesh.getNodeList().size());


    /*
        Capture mesh time again immediately before transmission.

        Usually this will only be slightly later than the
        measurement timestamp.
    */

    uint32_t transmitMeshTimeUs = mesh.getNodeTime();


    /*
        Build a JSON-style message.

        This format will also be convenient later when a computer
        or web dashboard needs to process the information.
    */

    String message;
    message.reserve(400);

    message += F("{");

    message += F("\"message_type\":\"uwb_sample\",");

    message += F("\"source\":\"");
    message += thisNodeName;
    message += F("\",");

    message += F("\"mac\":\"");
    message += thisNodeMac;
    message += F("\",");

    message += F("\"mesh_node_id\":");
    message += String(mesh.getNodeId());
    message += F(",");

    message += F("\"sequence\":");
    message += String(packetSequence);
    message += F(",");

    message += F("\"local_uptime_ms\":");
    message += String(localUptimeMs);
    message += F(",");

    message += F("\"measurement_mesh_time_us\":");
    message += String(measurementMeshTimeUs);
    message += F(",");

    message += F("\"transmit_mesh_time_us\":");
    message += String(transmitMeshTimeUs);
    message += F(",");

    message += F("\"time_adjustment_count\":");
    message += String(timeAdjustmentCount);
    message += F(",");

    message += F("\"last_time_adjustment_us\":");
    message += String(lastTimeAdjustmentUs);
    message += F(",");

    message += F("\"known_remote_nodes\":");
    message += String(knownRemoteNodes);
    message += F(",");

    message += F("\"fake_uwb\":{");

    message += F("\"tag_id\":\"TAG_1\",");

    message += F("\"distance_m\":");
    message += String(fakeDistanceMetres, 2);

    message += F("}");

    message += F("}");


    /*
        Broadcast the packet throughout the mesh.

        painlessMesh handles message forwarding through other
        nodes when a multi-hop route is necessary.
    */

    bool acceptedByMesh = mesh.sendBroadcast(message);


    Serial.println();
    Serial.println(F("--------------------------------------------------"));

    Serial.printf(
        "[%s TRANSMITTED PACKET %u]\n",
        thisNodeName.c_str(),
        packetSequence
    );

    Serial.printf(
        "Accepted by mesh:       %s\n",
        acceptedByMesh ? "YES" : "NO"
    );

    Serial.printf(
        "Local uptime:           %u ms\n",
        localUptimeMs
    );

    Serial.printf(
        "Measurement mesh time:  %u us\n",
        measurementMeshTimeUs
    );

    Serial.printf(
        "Transmit mesh time:     %u us\n",
        transmitMeshTimeUs
    );

    Serial.printf(
        "Fake TAG_1 distance:    %.2f m\n",
        fakeDistanceMetres
    );

    Serial.printf(
        "Known remote nodes:     %u\n",
        knownRemoteNodes
    );

    Serial.println(F("Complete transmitted message:"));
    Serial.println(message);

    Serial.println(F("--------------------------------------------------"));
}


// ============================================================
// CHECK WHETHER THIS ESP'S TRANSMISSION SLOT HAS ARRIVED
// ============================================================

void checkTransmissionSlot()
{
    /*
        Obtain the shared painlessMesh time.

        Every connected ESP attempts to maintain the same
        relative microsecond timebase.
    */

    uint32_t meshNowUs = mesh.getNodeTime();


    /*
        Determine which five-second frame the mesh is currently
        experiencing.
    */

    uint32_t currentFrame =
        meshNowUs / MESH_FRAME_US;


    /*
        Determine the current position within the five-second
        frame.

        This value repeatedly counts from zero to just under
        5,000,000 microseconds.
    */

    uint32_t positionInFrameUs =
        meshNowUs % MESH_FRAME_US;


    /*
        Calculate this ESP's assigned transmission slot.

        ESP1: 250,000 us
        ESP2: 1,250,000 us
        ESP3: 2,250,000 us
        ESP4: 3,250,000 us
    */

    uint32_t thisNodeSlotStartUs =
        FIRST_SLOT_OFFSET_US +
        ((static_cast<uint32_t>(thisNodeNumber) - 1UL)
        * SLOT_SPACING_US);


    /*
        Transmit only when:

        1. We are inside this ESP's assigned time window.
        2. This ESP has not already transmitted during the
           current five-second frame.
    */

    bool insideTransmissionWindow =
        positionInFrameUs >= thisNodeSlotStartUs &&
        positionInFrameUs <
            (thisNodeSlotStartUs + SLOT_WINDOW_US);

    bool notAlreadySentThisFrame =
        currentFrame != lastSentFrame;


    if (insideTransmissionWindow && notAlreadySentThisFrame)
    {
        /*
            Mark the frame as sent before broadcasting.

            This prevents duplicate transmissions if
            sendSamplePacket() takes long enough for loop() to
            run again inside the same transmission window.
        */

        lastSentFrame = currentFrame;

        sendSamplePacket();
    }
}


// ============================================================
// CALLED WHEN A MESSAGE IS RECEIVED
// ============================================================

void receivedCallback(uint32_t from, String &message)
{
    /*
        Record the local node's mesh time when the complete
        message was received.

        The message itself also contains the sender's measurement
        and transmission timestamps.
    */

    uint32_t receivedMeshTimeUs = mesh.getNodeTime();


    Serial.println();
    Serial.println(F("=================================================="));

    Serial.printf(
        "[MESSAGE RECEIVED BY %s]\n",
        thisNodeName.c_str()
    );

    Serial.printf(
        "Original sender node ID: %u\n",
        from
    );

    Serial.printf(
        "Received at mesh time:   %u us\n",
        receivedMeshTimeUs
    );

    Serial.println(F("Received packet:"));
    Serial.println(message);

    Serial.println(F("=================================================="));
}


// ============================================================
// CALLED WHEN A NEW LOCAL MESH CONNECTION IS CREATED
// ============================================================

void newConnectionCallback(uint32_t nodeId)
{
    Serial.println();

    Serial.printf(
        "[%s] New connection to mesh node %u\n",
        thisNodeName.c_str(),
        nodeId
    );

    Serial.printf(
        "[%s] Current mesh time: %u us\n",
        thisNodeName.c_str(),
        mesh.getNodeTime()
    );
}


// ============================================================
// CALLED WHEN THE MESH TOPOLOGY CHANGES
// ============================================================

void changedConnectionCallback()
{
    uint32_t knownRemoteNodes =
        static_cast<uint32_t>(mesh.getNodeList().size());

    String topology = mesh.subConnectionJson();


    Serial.println();
    Serial.println(F("##################################################"));

    Serial.printf(
        "[%s] MESH TOPOLOGY CHANGED\n",
        thisNodeName.c_str()
    );

    Serial.printf(
        "Known remote nodes: %u\n",
        knownRemoteNodes
    );

    Serial.println(F("Current topology:"));
    Serial.println(topology);

    Serial.println(F("##################################################"));
}


// ============================================================
// CALLED WHEN PAINLESSMESH ADJUSTS THIS ESP'S CLOCK
// ============================================================

void nodeTimeAdjustedCallback(int32_t offset)
{
    timeAdjustmentCount++;
    lastTimeAdjustmentUs = offset;


    Serial.println();

    Serial.printf(
        "[%s] Mesh clock synchronised\n",
        thisNodeName.c_str()
    );

    Serial.printf(
        "Current mesh time: %u us\n",
        mesh.getNodeTime()
    );

    Serial.printf(
        "Applied adjustment: %d us\n",
        offset
    );

    Serial.printf(
        "Total adjustments: %u\n",
        timeAdjustmentCount
    );
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);
    delay(500);


    Serial.println();
    Serial.println();
    Serial.println(F("=================================================="));
    Serial.println(F("STARTING FOUR-ESP PAINLESSMESH TEST"));
    Serial.println(F("=================================================="));


    /*
        Identify whether this physical board is ESP1, ESP2,
        ESP3 or ESP4.
    */

    identifyThisESP();


    Serial.printf(
        "Detected board:       %s\n",
        thisNodeName.c_str()
    );

    Serial.printf(
        "Station MAC address:  %s\n",
        thisNodeMac.c_str()
    );


    if (thisNodeName == "UNKNOWN")
    {
        Serial.println();
        Serial.println(F("WARNING"));
        Serial.println(F("This MAC address is not in the stored list."));
        Serial.println(F("Check the MAC address shown above."));
        Serial.println(F("The board will still attempt to join the mesh."));
    }


    /*
        Show serious errors and startup information.

        This must be called before mesh.init().
    */

    mesh.setDebugMsgTypes(ERROR | STARTUP);


    /*
        Start the painlessMesh network.

        Every board must use the same mesh name, password and
        port.
    */

    mesh.init(
        MESH_PREFIX,
        MESH_PASSWORD,
        &userScheduler,
        MESH_PORT
    );


    /*
        Register the functions that painlessMesh should call
        when different mesh events happen.
    */

    mesh.onReceive(&receivedCallback);

    mesh.onNewConnection(&newConnectionCallback);

    mesh.onChangedConnections(&changedConnectionCallback);

    mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);


    Serial.println();
    Serial.printf(
        "Friendly name:         %s\n",
        thisNodeName.c_str()
    );

    Serial.printf(
        "painlessMesh node ID:  %u\n",
        mesh.getNodeId()
    );

    Serial.printf(
        "Initial mesh time:     %u us\n",
        mesh.getNodeTime()
    );

    Serial.printf(
        "Mesh network name:     %s\n",
        MESH_PREFIX
    );

    Serial.println();
    Serial.println(F("The mesh is now operating."));
    Serial.println(F("Waiting for other ESP32 nodes..."));
    Serial.println(F("=================================================="));
}


// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{
    /*
        This must run continuously.

        It maintains connections, receives messages, sends
        messages and performs mesh clock synchronisation.

        Do not place long delay() calls in this loop.
    */

    mesh.update();


    /*
        Check the synchronised mesh clock and transmit when this
        board's assigned time slot arrives.
    */

    checkTransmissionSlot();
}