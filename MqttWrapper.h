#ifndef MQTT_WRAPPER_H
#define MQTT_WRAPPER_H

#include "PubSubClient.h"
// #include <Arduino.h>
#include <ArduinoJson.h>
#include "ESP8266WiFi.h"
#include <functional>

#ifdef DEBUG_MODE
#ifndef DEBUG_PRINTER
#define DEBUG_PRINTER Serial
#endif
#define PRODUCTION_MODE

#define DEBUG_PRINT(...) { DEBUG_PRINTER.print(__VA_ARGS__); }
#define DEBUG_PRINTLN(...) { DEBUG_PRINTER.println(__VA_ARGS__); }
#else
#define DEBUG_PRINT(...) {}
#define DEBUG_PRINTLN(...) {}
#endif




class MqttWrapper
{
public:
    typedef struct {
        MQTT::Connect *connOpts;
        PubSubClient *client;
        String* clientId;
        String* topicSub;
        String* topicPub;
    } Config;


    typedef void (*callback_t)(void);
    typedef void (*callback_with_arg_t)(void*);
    typedef std::function<void(const MqttWrapper::Config)> cmmc_config_t;
    typedef std::function<void(JsonObject** )> publish_hook_t;

    MqttWrapper(const char* , int port = 1883);
    MqttWrapper(const char* , int port, cmmc_config_t config_hook);
    ~MqttWrapper();

    void initConfig(const char*, int);
    void sync_pub(String payload) {
        MQTT::Publish newpub(topicSub, (uint8_t*)payload.c_str(), payload.length());
        client->publish(newpub);        
    }

    void connect(PubSubClient::callback_t callback = NULL) {
        DEBUG_PRINTLN("BEGIN Wrapper");
        setDefaultClientId();

        _hook_config();

        client = new PubSubClient(_mqtt_host, _mqtt_port);
        connOpts = new MQTT::Connect(clientId);


        if (callback != NULL) {
            DEBUG_PRINTLN("__USER REGISTER SUBSCRIOTION CALLBACK");
            _user_callback = callback;

            client->set_callback([&](const MQTT::Publish& pub) {
                if (_user_callback != NULL) {
                    _user_callback(pub);
                }
            });
        }
        else {
            DEBUG_PRINTLN("__USER DOES NOT REGISTER SUBSCRIOTION CALLBACk");
        }

        _connect();

    }

    void _hook_config() {
        _config.connOpts = connOpts;
        _config.client = client;
        _config.clientId = &(this->clientId);
        _config.topicSub = &(this->topicSub);
        _config.topicPub = &(this->topicPub);


        DEBUG_PRINTLN("DOING HOOKCONFIG");
        if (_user_hook_config != NULL) {
            DEBUG_PRINTLN("IN HOOK CONFIG");
            _user_hook_config(_config);
        }
        else {
            DEBUG_PRINTLN("NOT IN NOT IN HOOK CONFIG");
        }
    }

    void set_configuration_hook(cmmc_config_t func) {
        _user_hook_config = func;
    }

    void set_prepare_publish_data_hook(publish_hook_t func, unsigned long publish_interval = 3000) {
        _user_hook_before_publish = func;
        _publish_interval = publish_interval;
    }


    void loop() {
        if (client->loop())
        {
            doPublish();
        }
        else
        {
            DEBUG_PRINTLN("MQTT DISCONNECTED");
            _connect();
        }

    }

protected:
    void setDefaultClientId() {
        clientId = ESP.getChipId();

        uint8_t mac[6];
        WiFi.macAddress(mac);
        String result;
        for (int i = 0; i < 6; ++i)
        {
            result += String(mac[i], 16);
            if (i < 5)
                result += ':';
        }

        DEBUG_PRINT("MAC ADDR: ");
        DEBUG_PRINTLN(result);

        topicSub = String("esp8266-") + result + String("/command");
        topicPub = String("esp8266-") + result + String("/status");

    }

    const char* getClientId()
    {
        return clientId.c_str();
    }

    void beforePublish(char** ptr) {
        DEBUG_PRINTLN("__CALL BEFORE PUBLISH DATA");

        if (_user_hook_before_publish != NULL) {
            DEBUG_PRINTLN("__USER_HOOK_BEFORE_PUBLISH()");
            _user_hook_before_publish(&root);
        }
        // DEBUG_PRINTLN("BEFORE PUBLISH");
    }

    void afterPublish(char** ptr) {
        // DEBUG_PRINTLN("AFTER PUBLISH");
    }

    void doPublish() {
        static long counter = 0;
        char *dataPtr = "";
        if (millis() - prev_millis > _publish_interval) {
            // prepareJson(&dataPtr);
            beforePublish(&dataPtr);
            (*d)["counter"] = ++counter;
            (*d)["heap"] = ESP.getFreeHeap();
            (*d)["seconds"] = millis()/1000;

            root->printTo(jsonStrbuffer, sizeof(jsonStrbuffer));
            dataPtr = jsonStrbuffer;
            prev_millis = millis();
            DEBUG_PRINT("__DO PUBLISH --> ");
            while (!client->publish(topicPub, jsonStrbuffer))
            {
                delay(500);
                DEBUG_PRINTLN("__PUBLISHED ERROR.");
            }
            DEBUG_PRINT(dataPtr);
            DEBUG_PRINTLN(" PUBLISHED!");
            afterPublish(&dataPtr);
        }
    }




protected:

private:
    cmmc_config_t _user_hook_config;
    publish_hook_t _user_hook_before_publish;

    String _mqtt_host = "x";
    int _mqtt_port = 0;
    int _publish_interval = 3000;
    Config _config;

    String clientId;
    String topicSub;
    String topicPub;

    MQTT::Connect *connOpts;
    PubSubClient *client;

    PubSubClient::callback_t _user_callback;

    unsigned long prev_millis;

    // const int BUFFER_SIZE = JSON_OBJECT_SIZE(3) + JSON_ARRAY_SIZE(2);

    StaticJsonBuffer<512> jsonBuffer;
    char jsonStrbuffer[512];
    JsonObject *root;
    JsonObject *d;



    void _connect() {
        DEBUG_PRINTLN("== Wrapper.connect(); CONNECT WITH OPTIONS = ");
        DEBUG_PRINT("HOST: ");
        DEBUG_PRINTLN(_mqtt_host);
        DEBUG_PRINT("PORT: ");
        DEBUG_PRINTLN(_mqtt_port);
        // DEBUG_PRINT("topicSub: ");
        // DEBUG_PRINTLN(_mqtt_port);
        DEBUG_PRINT("clientId: ");
        DEBUG_PRINTLN(clientId);
        

        client->set_max_retries(150);
        while(!client->connect(*connOpts)) {
            DEBUG_PRINTLN("connecting...");
            delay(100);
        }

        DEBUG_PRINTLN("CONNECTED");


        if (_user_callback != NULL) {
            DEBUG_PRINT("SUBSCRIBING...");
            DEBUG_PRINTLN(topicSub);
            while(!client->subscribe(topicSub)) {
                DEBUG_PRINT("subscribing...");
                DEBUG_PRINTLN(topicSub);
                delay(100);
            };
            DEBUG_PRINTLN("subscribeed");
        }
    }

};



#endif//MQTT_WRAPPER_H