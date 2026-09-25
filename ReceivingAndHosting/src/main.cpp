#include <Arduino.h>
#include <painlessMesh.h>

#define MESH_PREFIX   "MiningMesh"
#define MESH_PASSWORD "MineMesh2026"
#define MESH_PORT     5555

Scheduler scheduler;
painlessMesh mesh;

void receivedCallback(uint32_t from, String &msg)
{
    Serial.println();
    Serial.print("Packet received from node ");
    Serial.println(from);

    Serial.print("Message: ");
    Serial.println(msg);
}

void newConnectionCallback(uint32_t nodeId)
{
    Serial.print("New mesh connection: ");
    Serial.println(nodeId);
}

void changedConnectionCallback()
{
    Serial.println("Mesh connections changed.");
}

void setup()
{
    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("======================");
    Serial.println("BASE MESH NODE STARTING");
    Serial.println("======================");

    mesh.setDebugMsgTypes(
        ERROR | STARTUP
    );

    mesh.init(
        MESH_PREFIX,
        MESH_PASSWORD,
        &scheduler,
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

    Serial.print("Mesh Node ID: ");
    Serial.println(
        mesh.getNodeId()
    );

    Serial.println();
    Serial.println(
        "Waiting for mesh packets..."
    );
}

void loop()
{
    mesh.update();
}