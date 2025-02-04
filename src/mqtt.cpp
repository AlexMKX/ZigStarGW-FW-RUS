#include <Arduino.h>
#include <ArduinoJson.h>
#include <ETH.h>
#include "WiFi.h"
#include <WebServer.h>
#include "FS.h"
#include "FS.h"
#include <LittleFS.h>
#include "web.h"
#include "config.h"
#include "log.h"
#include "etc.h"
#include <PubSubClient.h>
#include "mqtt.h"
#include <Syslog.h>

extern struct ConfigSettingsStruct ConfigSettings;
extern Syslog syslog;
WiFiClient clientMqtt;

PubSubClient clientPubSub(clientMqtt);

void mqttConnectSetup()
{
    WiFi.hostByName(ConfigSettings.mqttServer,ConfigSettings.mqttServerIP);
    clientPubSub.setServer(ConfigSettings.mqttServerIP, ConfigSettings.mqttPort);
    clientPubSub.setCallback(mqttCallback);
}

void mqttReconnect()
{
    DEBUG_PRINT(F("Attempting MQTT connection..."));

    byte willQoS = 0;
    String willTopic = String(ConfigSettings.mqttTopic) + "/avty";
    const char *willMessage = "offline";
    boolean willRetain = false;
    String clientID;
    getDeviceID(clientID);

    if (clientPubSub.connect(clientID.c_str(), ConfigSettings.mqttUser, ConfigSettings.mqttPass, willTopic.c_str(), willQoS, willRetain, willMessage))
    {
        ConfigSettings.mqttReconnectTime = 0;
        mqttOnConnect();
    }
    else
    {
        
        DEBUG_PRINT(F("failed, rc="));
        DEBUG_PRINT(clientPubSub.state());
        DEBUG_PRINT(F(" try again in "));
        DEBUG_PRINT(ConfigSettings.mqttInterval);
        DEBUG_PRINTLN(F(" seconds"));
        syslog.logf("failed with %d try again in %d seconds",clientPubSub.state(),ConfigSettings.mqttInterval);
        mqttConnectSetup();
        ConfigSettings.mqttReconnectTime = millis() + ConfigSettings.mqttInterval * 1000;
    }
}

void mqttOnConnect()
{
    DEBUG_PRINTLN(F("connected"));
    mqttSubscribe("cmd");
    DEBUG_PRINTLN(F("mqtt Subscribed"));
    if (ConfigSettings.mqttDiscovery)
    {
        mqttPublishDiscovery();
        DEBUG_PRINTLN(F("mqtt Published Discovery"));
    }

    mqttPublishIo("rst_esp", "OFF");
    mqttPublishIo("rst_zig", "OFF");
    mqttPublishIo("enbl_bsl", "OFF");
    mqttPublishIo("socket", "OFF");
    DEBUG_PRINTLN(F("mqtt Published IOs"));
    mqttPublishAvty();
    DEBUG_PRINTLN(F("mqtt Published Avty"));
    if (ConfigSettings.mqttInterval > 0)
    {
        mqttPublishState();
        DEBUG_PRINTLN(F("mqtt Published State"));
    }
}

void mqttPublishMsg(String topic, String msg, bool retain)
{
    clientPubSub.beginPublish(topic.c_str(), msg.length(), retain);
    clientPubSub.print(msg.c_str());
    clientPubSub.endPublish();
}

void mqttPublishAvty()
{
    String topic(ConfigSettings.mqttTopic);
    topic = topic + "/avty";
    String mqttBuffer = "online";
    clientPubSub.publish(topic.c_str(), mqttBuffer.c_str(), true);
}

void mqttPublishState()
{
    String topic(ConfigSettings.mqttTopic);
    topic = topic + "/state";
    DynamicJsonDocument root(1024);
    String readableTime;
    getReadableTime(readableTime, 0);
    root["uptime"] = readableTime;

    float CPUtemp = getCPUtemp();
    root["temperature"] = String(CPUtemp);

    
    //if (ConfigSettings.board == 2) {
        //String OWWstrg;
    float temp_OW = oneWireRead();
    if (temp_OW) {
        root["ow_temperature"] = String(temp_OW);
    }
    else {
        root["ow_temperature"] = NULL;
    }
    //}
    
    root["connections"] = ConfigSettings.connectedClients;
    
    if (ConfigSettings.connectedEther)
    {
        if (ConfigSettings.dhcp)
        {
            root["ip"] = ETH.localIP().toString();
        }
        else
        {
            root["ip"] = ConfigSettings.ipAddress;
        }
    }
    else
    {
        if (ConfigSettings.dhcpWiFi)
        {
            root["ip"] = WiFi.localIP().toString();
        }
        else
        {
            root["ip"] = ConfigSettings.ipAddressWiFi;
        }
    }
    if (ConfigSettings.emergencyWifi)
    {
        root["emergencyMode"] = "ON";
    }
    else
    {
        root["emergencyMode"] = "OFF";
    }
    root["hostname"] = ConfigSettings.hostname;
    String mqttBuffer;
    serializeJson(root, mqttBuffer);
    DEBUG_PRINTLN(mqttBuffer);
    clientPubSub.publish(topic.c_str(), mqttBuffer.c_str(), true);
    ConfigSettings.mqttHeartbeatTime = millis() + (ConfigSettings.mqttInterval * 1000);
}

void mqttPublishIo(String const &io, String const &state)
{
    if (clientPubSub.connected())
    {
        String topic(ConfigSettings.mqttTopic);
        topic = topic + "/io/" + io;
        clientPubSub.publish(topic.c_str(), state.c_str(), true);
    }
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    char jjson[length + 1];
    memcpy(jjson, payload, length);
    jjson[length + 1] = '\0';

    DynamicJsonDocument jsonBuffer(1024);

    deserializeJson(jsonBuffer, jjson);

    const char *command = jsonBuffer["cmd"];

    DEBUG_PRINT(F("mqtt Callback - "));
    DEBUG_PRINTLN(jjson);

    if (command) {
        DEBUG_PRINT(F("mqtt cmd - "));
        DEBUG_PRINTLN(command);
        if (strcmp(command, "rst_esp") == 0)
        {
            printLogMsg("ESP restart MQTT");
            ESP.restart();
        }

        if (strcmp(command, "rst_zig") == 0)
        {
            printLogMsg("Zigbee restart MQTT");
            zigbeeRestart();
        }

        if (strcmp(command, "enbl_bsl") == 0)
        {
            printLogMsg("Zigbee BSL enable MQTT");
            zigbeeEnableBSL();
        }
    }
    return;
}

void mqttSubscribe(String topic)
{
    String mtopic(ConfigSettings.mqttTopic);
    mtopic = mtopic + "/" + topic;
    clientPubSub.subscribe(mtopic.c_str());
}

void mqttLoop()
{
    if (!clientPubSub.connected())
    {
        if (ConfigSettings.mqttReconnectTime == 0)
        {
            //mqttReconnect();
            DEBUG_PRINTLN(F("mqttReconnect in 5 seconds"));
            ConfigSettings.mqttReconnectTime = millis() + 5000;
        }
        else
        {
            if (ConfigSettings.mqttReconnectTime <= millis())
            {
                mqttReconnect();
            }
        }
    }
    else
    {
        clientPubSub.loop();
        if (ConfigSettings.mqttInterval > 0)
        {
            if (ConfigSettings.mqttHeartbeatTime <= millis())
            {
                mqttPublishState();
            }
        }
    }
}

void mqttPublishDiscovery()
{
    String mtopic(ConfigSettings.mqttTopic);
    String deviceName = mtopic; //ConfigSettings.hostname;
    String discTopic = ConfigSettings.mqttDiscoveryTopic;
    String topic;
    DynamicJsonDocument buffJson(2048);
    String mqttBuffer;

    DynamicJsonDocument via(1024);
    via["ids"] = ETH.macAddress();

    int lastAutoMsg = 10;
    //if (ConfigSettings.board == 2) lastAutoMsg--;

    for (int i = 0; i <= lastAutoMsg; i++)
    {
        switch (i)
        {
            case 0:
            {
                DynamicJsonDocument dev(1024);
                dev["ids"] = ETH.macAddress();
                dev["name"] = ConfigSettings.hostname;
                dev["mf"] = "Zig Star";
                dev["mdl"] = ConfigSettings.boardName;
                #ifdef DEBUG
                dev["sw"] = String(VERSION) + " DEBUG";
                #else
                dev["sw"] = VERSION;
                #endif

                topic = discTopic + "/button/" + deviceName + "/rst_esp/config";
                buffJson["name"] = "Перезагрузить ESP";
                buffJson["uniq_id"] = deviceName + "/rst_esp";
                buffJson["stat_t"] = mtopic + "/io/rst_esp";
                buffJson["cmd_t"] = mtopic + "/cmd";
                buffJson["icon"] = "mdi:restore-alert";
                buffJson["payload_press"] = "{cmd:\"rst_esp\"}";
                //buffJson["pl_off"] = "{cmd:\"rst_esp\"}";
                buffJson["avty_t"] = mtopic + "/avty";
                buffJson["dev"] = dev;
                break;
            }
            case 1:
            {
                topic = discTopic + "/button/" + deviceName + "/rst_zig/config";
                buffJson["name"] = "Перезагрузить Zigbee";
                buffJson["uniq_id"] = deviceName + "/rst_zig";
                buffJson["stat_t"] = mtopic + "/io/rst_zig";
                buffJson["cmd_t"] = mtopic + "/cmd";
                buffJson["icon"] = "mdi:restart";
                buffJson["payload_press"] = "{cmd:\"rst_zig\"}";
                //buffJson["pl_off"] = "{cmd:\"rst_zig\"}";
                buffJson["avty_t"] = mtopic + "/avty";
                buffJson["dev"] = via;
                break;
            }
            case 2:
            {
                topic = discTopic+ "/button/" + deviceName + "/enbl_bsl/config";
                buffJson["name"] = "Включить BSL";
                buffJson["uniq_id"] = deviceName + "/enbl_bsl";
                buffJson["stat_t"] = mtopic + "/io/enbl_bsl";
                buffJson["cmd_t"] = mtopic + "/cmd";
                buffJson["icon"] = "mdi:flash";
                buffJson["payload_press"] = "{cmd:\"enbl_bsl\"}";
                //buffJson["pl_off"] = "{cmd:\"enbl_bsl\"}";
                buffJson["avty_t"] = mtopic + "/avty";
                buffJson["dev"] = via;
                break;
            }
            case 3:
            {
                topic = discTopic+ "/binary_sensor/" + deviceName + "/socket/config";
                buffJson["name"] = "Сокет";
                buffJson["uniq_id"] = deviceName + "/socket";
                buffJson["stat_t"] = mtopic + "/io/socket";
                buffJson["avty_t"] = mtopic + "/avty";
                buffJson["dev_cla"] = "connectivity";
                buffJson["dev"] = via;
                break;
            }
            case 4:
            {
                topic = discTopic + "/binary_sensor/" + deviceName + "/emrgncMd/config";
                buffJson["name"] = "Аварийный режим";
                buffJson["uniq_id"] = deviceName + "/emrgncMd";
                buffJson["stat_t"] = mtopic + "/state";
                buffJson["avty_t"] = mtopic + "/avty";
                buffJson["val_tpl"] = "{{ value_json.emergencyMode }}";
                buffJson["json_attr_t"] = mtopic + "/state";
                buffJson["dev_cla"] = "power";
                buffJson["icon"] = "mdi:access-point-network";
                buffJson["dev"] = via;
                break;
            }
            case 5:
            {
                topic = discTopic+ "/sensor/" + deviceName + "/uptime/config";
                buffJson["name"] = "В работе";
                buffJson["uniq_id"] = deviceName + "/uptime";
                buffJson["stat_t"] = mtopic + "/state";
                buffJson["avty_t"] = mtopic + "/avty";
                buffJson["val_tpl"] = "{{ value_json.uptime }}";
                buffJson["json_attr_t"] = mtopic + "/state";
                buffJson["icon"] = "mdi:clock";
                buffJson["dev"] = via;
                break;
            }
            case 6:
            {
                topic = discTopic+ "/sensor/" + deviceName + "/ip/config";
                buffJson["name"] = "IP";
                buffJson["uniq_id"] = deviceName + "/ip";
                buffJson["stat_t"] = mtopic + "/state";
                buffJson["avty_t"] = mtopic + "/avty";
                buffJson["val_tpl"] = "{{ value_json.ip }}";
                buffJson["json_attr_t"] = mtopic + "/state";
                buffJson["icon"] = "mdi:check-network";
                buffJson["dev"] = via;
                break;
            }
            case 7:
            {
                topic = discTopic+ "/sensor/" + deviceName + "/temperature/config";
                buffJson["name"] = "ESP температура";
                buffJson["uniq_id"] = deviceName + "/temperature";
                buffJson["stat_t"] = mtopic + "/state";
                buffJson["avty_t"] = mtopic + "/avty";
                buffJson["val_tpl"] = "{{ value_json.temperature }}";
                buffJson["json_attr_t"] = mtopic + "/state";
                buffJson["icon"] = "mdi:coolant-temperature";
                buffJson["dev"] = via;
                buffJson["dev_cla"] = "temperature";
                buffJson["stat_cla"] = "measurement";
                buffJson["unit_of_meas"] = "°C";
                break;
            }
            case 8:
            {
                topic = discTopic+ "/sensor/" + deviceName + "/hostname/config";
                buffJson["name"] = "Имя хоста";
                buffJson["uniq_id"] = deviceName + "/hostname";
                buffJson["stat_t"] = mtopic + "/state";
                buffJson["avty_t"] = mtopic + "/avty";
                buffJson["val_tpl"] = "{{ value_json.hostname }}";
                buffJson["json_attr_t"] = mtopic + "/state";
                buffJson["icon"] = "mdi:account-network";
                buffJson["dev"] = via;
                break;
            }
            case 9:
            {
                topic = "discovery/sensor/" + deviceName + "/connections/config";
                buffJson["name"] = "Подключений";
                buffJson["uniq_id"] = deviceName + "/connections";
                buffJson["stat_t"] = mtopic + "/state";
                buffJson["avty_t"] = mtopic + "/avty";
                buffJson["val_tpl"] = "{{ value_json.connections }}";
                buffJson["json_attr_t"] = mtopic + "/state";
                buffJson["icon"] = "mdi:check-network-outline";
                buffJson["dev"] = via;
                break;
            }
            case 10:
            {
                topic = discTopic+ "/sensor/" + deviceName + "/ow_temperature/config";
                buffJson["name"] = "OW температура";
                buffJson["uniq_id"] = deviceName + "/ow_temperature";
                buffJson["stat_t"] = mtopic + "/state";
                buffJson["avty_t"] = mtopic + "/avty";
                buffJson["val_tpl"] = "{{ value_json.ow_temperature }}";
                buffJson["json_attr_t"] = mtopic + "/state";
                buffJson["icon"] = "mdi:coolant-temperature";
                buffJson["dev"] = via;
                buffJson["dev_cla"] = "temperature";
                buffJson["stat_cla"] = "measurement";
                buffJson["unit_of_meas"] = "°C";
                break;
            }
        }
        if (i == 10) {
            //if (ConfigSettings.board == 2) {
            float temp_OW = oneWireRead();
            if (temp_OW == false) {
                break;
            }
            //}
            //else {
            //    break;
            //}
        }
        serializeJson(buffJson, mqttBuffer);
        //DEBUG_PRINTLN(mqttBuffer);
        mqttPublishMsg(topic, mqttBuffer, true);
        buffJson.clear();
        mqttBuffer = "";
    }
}