/*

Copyright Nat Weerawan 2015-2016
MIT License

The MIT License (MIT)

Copyright (c) Nat Weerawan <nat.wrw@gmail.com> (cmmakerclub.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include "MqttConnector.h"

static MqttConnector *_mqtt = NULL;
MqttConnector::MqttConnector(const char* host, uint16_t port)
{
    _on_message_arrived = [&](const MQTT::Publish& pub) {
        if (_user_on_message_arrived) {
            _user_on_message_arrived(pub);
        }
    };

    init_config(host, port);

    MQTT_DEBUG_PRINTLN("----------- Wrapper CONSTRUCTOR ---------");
    MQTT_DEBUG_PRINT(_mqtt_host);
    MQTT_DEBUG_PRINT(" - ");
    MQTT_DEBUG_PRINT(_mqtt_port);
    MQTT_DEBUG_PRINTLN("---------- /Wrapper CONSTRUCTOR ---------");
}

void MqttConnector::init_config(const char* host, uint16_t port)
{

    prev_millis = millis();

    _mqtt_host = String(host);
    _mqtt_port = port;

    _config.connOpts = NULL;
    this->_client = NULL;

    _subscribe_object = NULL;
    _subscribe_object = new MQTT::Subscribe();

    _config.enableLastWill = true;
    _config.publishOnly = false;
    _config.subscribeOnly = false;
    _config.retainPublishMessage = false;
    _config.firstCapChannel = false;

    JsonObject& r = _jsonRootBuffer.createObject();
    JsonObject& info = r.createNestedObject("info");
    JsonObject& dd = _jsonDBuffer.createObject();

    this->_root = &r;
    this->_info = &info;


    r["info"] = info;
    r["d"] = dd;
    // this->d = &((JsonObject)r["d"]);
    this->_d = &dd;
    static struct station_config conf;
    wifi_station_get_config(&conf);
    const char* ssid = reinterpret_cast<const char*>(conf.ssid);

    info["ssid"] =  ssid;
    info["flash_size"] = ESP.getFlashChipSize();
    info["flash_id"] = String(ESP.getFlashChipId(), HEX);
    info["chip_id"] = String(ESP.getChipId(), HEX);
    info["sdk"] = system_get_sdk_version();

}

MqttConnector::MqttConnector(const char* host, uint16_t port, cmmc_config_t config_hook)
{
    init_config(host, port);
    _user_hook_config = config_hook;
}

void MqttConnector::_clear_last_will() {
    MQTT_DEBUG_PRINTLN("__CLEAR LASTWILL");
    MQTT_DEBUG_PRINT("WILL TOPIC: ");
    MQTT_DEBUG_PRINTLN(_config.topicLastWill);
    String willText = String("ONLINE|") + String(_config.clientId) + "|" + (millis()/1000);
    uint8_t* payload = (uint8_t*) willText.c_str();
    MQTT::Publish newpub(_config.topicLastWill, payload, willText.length());
    newpub.set_retain(true);
    this->_client->publish(newpub);

}

void MqttConnector::on_message(PubSubClient::callback_t callback) {
    if (callback != NULL)
    {
        MQTT_DEBUG_PRINTLN("__USER REGISTER SUBSCRIPTION CALLBACK");
        _user_on_message_arrived = callback;
    }
    else
    {
        MQTT_DEBUG_PRINTLN("__USER DOES NOT REGISTER SUBSCRIPTION CALLBACk");
    }
}

void MqttConnector::on_published(PubSubClient::callback_t callback) {
    if (callback != NULL)
    {
        MQTT_DEBUG_PRINTLN("__USER REGISTER SUBSCRIPTION CALLBACK");
        _user_on_published = callback;
    }
    else
    {
        MQTT_DEBUG_PRINTLN("__USER DOES NOT REGISTER SUBSCRIPTION CALLBACk");
    }
}


void MqttConnector::connect()
{
    MQTT_DEBUG_PRINTLN("BEGIN CMMC_MQTT_CONNECTOR");
    _set_default_client_id();
    _hook_config();
    _connect();
}


void MqttConnector::_hook_config()
{
    if (_user_hook_config != NULL)
    {
        MQTT_DEBUG_PRINTLN("OVERRIDE CONFIG IN _hook_config");
        _user_hook_config(&_config);
    }


    if (_config.clientId == "") {
      _config.clientId = String(ESP.getChipId());
    }

    String commandChannel = "/command";
    String statusChannel = "/status";
    String lwtChannel = "/online";

    if (_config.firstCapChannel) {
      commandChannel = "/Command";
      statusChannel = "/Status";
      lwtChannel = "/Online";
    }

    _config.topicSub = _config.channelPrefix + String("/") + _config.clientId + commandChannel;
    _config.topicPub = _config.channelPrefix + String("/") + _config.clientId + statusChannel;
    _config.topicLastWill = _config.channelPrefix + String("/") + _config.clientId + lwtChannel;


    (*this->_info)["id"] = _config.clientId.c_str();;
    (*this->_info)["prefix"] = _config.channelPrefix.c_str();


    _config.mqttHost = _mqtt_host;
    _config.mqttPort = _mqtt_port;

    MQTT_DEBUG_PRINT("__PUBLICATION TOPIC -> ");

    #ifdef MQTT_DEBUG_LEVEL_VERBOSE
    MQTT_DEBUG_PRINTLN(_config.topicPub)
    #endif
    MQTT_DEBUG_PRINT("__SUBSCRIPTION TOPIC -> ");
    #ifdef MQTT_DEBUG_LEVEL_VERBOSE
    MQTT_DEBUG_PRINTLN(_config.topicSub);
    #endif

    _config.connOpts = new MQTT::Connect(_config.clientId);
    _config.client = new PubSubClient(wclient);
    this->_client = _config.client;

    this->_client->set_server(_mqtt_host, _mqtt_port);
    this->_client->set_callback(_on_message_arrived);

    _config.username.trim();
    _config.password.trim();

    // NO-NEED TO SET AUTH;
    if(_config.username == "" ||  _config.password == "") {
        MQTT_DEBUG_PRINT("NO-AUTH Connection.");
    }
    else {
        _config.connOpts->set_auth(_config.username, _config.password);
    }


    if (_config.enableLastWill) {
        MQTT_DEBUG_PRINT("ENABLE LAST WILL: ");
        String willText = String("DEAD|") + String(_config.clientId) + "|" + (millis()/1000);
        int qos = 1;
        int retain = true;
        (_config.connOpts)->set_will(_config.topicLastWill, willText, qos, retain);
        (_config.connOpts)->set_clean_session(false);
        (_config.connOpts)->set_keepalive(15);
        #ifdef MQTT_DEBUG_LEVEL_VERBOSE
        MQTT_DEBUG_PRINTLN(_config.topicLastWill);
        #endif
    }

}

void MqttConnector::sync_pub(String payload)
{
    MQTT_DEBUG_PRINT("SYNC PUB.... -> ");
    MQTT_DEBUG_PRINTLN(payload.c_str());

    MQTT::Publish newpub(_config.topicSub, (uint8_t*)payload.c_str(), payload.length());
    newpub.set_retain(true);
    this->_client->publish(newpub);
}


void MqttConnector::loop()
{
  if (!this->_is_connecting) {
    if (this->_client->connected())
    {
        _do_publish();
    }
    else
    {
        MQTT_DEBUG_PRINTLN("MQTT NOT CONNECTED.");
        MQTT_DEBUG_PRINTLN("Reconnecting.......");
        _connect();
    }
  }
}

void MqttConnector::_do_publish(bool force)
{
    static long counter = 0;

    if (force || _timer_expired(&publish_timer))
    {
        if (!this->_client->connected()) return;
        _timer_set(&publish_timer, _publish_interval);

        _prepare_data_hook();

        (*this->_d)["version"] = _version.c_str();
        IPAddress ip = WiFi.localIP();
        String ipStr = String(ip[0]) + '.' + String(ip[1]) +
                       '.' + String(ip[2]) + '.' + String(ip[3]);
        (*this->_d)["heap"] = ESP.getFreeHeap();
        (*this->_d)["ip"] = ipStr.c_str();
        (*this->_d)["rssi"] = WiFi.RSSI();
        (*this->_d)["counter"] = ++counter;
        (*this->_d)["seconds"] = millis()/1000;
        (*this->_d)["subscription"] = String(_subscription_counter).c_str();


        _after_prepare_data_hook();


        strcpy(jsonStrbuffer, "");
        this->_root->printTo(jsonStrbuffer, sizeof(jsonStrbuffer));
        // dataPtr = jsonStrbuffer;
        prev_millis = millis();

        MQTT_DEBUG_PRINTLN("__TO BE PUBLISHED ");
        MQTT_DEBUG_PRINT("______________ TOPIC: -->");
        MQTT_DEBUG_PRINT(_config.topicPub);
        MQTT_DEBUG_PRINTLN();

        #ifdef MQTT_DEBUG_LEVEL_VERBOSE
        MQTT_DEBUG_PRINT("______________ CONTENT: -->");
        MQTT_DEBUG_PRINT(jsonStrbuffer);
        #endif
        MQTT_DEBUG_PRINTLN();

        // if (_user_hook_publish_data != NULL)
        // {
        //     _user_hook_publish_data(dataPtr);
        // }

        MQTT::Publish newpub(_config.topicPub, (uint8_t*)jsonStrbuffer, strlen(jsonStrbuffer));
        if (_config.retainPublishMessage) {
            newpub.set_retain(true) ;
        }
        if(!this->_client->publish(newpub)) {
            MQTT_DEBUG_PRINTLN();
            MQTT_DEBUG_PRINTLN("PUBLISHED FAILED!");
            return;
        }
        else {
            MQTT_DEBUG_PRINTLN();
            MQTT_DEBUG_PRINTLN("PUBLISHED SUCCEEDED!");
            if (_user_on_published) {
                _user_on_published(newpub);
            }

        }

        MQTT_DEBUG_PRINTLN("====================================");
    }
}


void MqttConnector::_connect()
{
    bool flag = true;
    this->_is_connecting = true;

    uint16_t times = 0;

    MQTT_DEBUG_PRINTLN("== TRY CONNECT TO MQTT BROKER ==");
    MQTT_DEBUG_PRINTLN("== Wrapper.connect(); CONNECT WITH OPTIONS = ");
    MQTT_DEBUG_PRINT("HOST: ");
    MQTT_DEBUG_PRINTLN(_mqtt_host);
    MQTT_DEBUG_PRINT("PORT: ");
    MQTT_DEBUG_PRINTLN(_mqtt_port);
    MQTT_DEBUG_PRINT("clientId: ");
    MQTT_DEBUG_PRINTLN(_config.clientId);

    MQTT_DEBUG_PRINT("USER: ");
    MQTT_DEBUG_PRINTLN(_config.username);
    MQTT_DEBUG_PRINT("PASS: ");
    MQTT_DEBUG_PRINTLN(_config.password);
    MQTT_DEBUG_PRINT("clientId: ");
    MQTT_DEBUG_PRINTLN(_config.clientId);

    MQTT_DEBUG_PRINT("lastWill: ");
    MQTT_DEBUG_PRINTLN(_config.enableLastWill);


    while(!this->_client->connect(*(_config.connOpts)) && flag)
    {
        MQTT_DEBUG_PRINTLN("STILL.. CONNECTING...");
        if (_user_hook_connecting) {
            MQTT_DEBUG_PRINTLN("Calling hook_connecting..");
            _user_hook_connecting(++times, &flag);
            MQTT_DEBUG_PRINTLN("TO BE YIELDED.");
            yield();
            MQTT_DEBUG_PRINTLN("YIELDED.");
        }
        else {
            MQTT_DEBUG_PRINTLN("TRY CONNECTING WITHOUT CONNECTING CALLBACK..");
            delay(100);
        }
    }

    MQTT_DEBUG_PRINTLN("CONNECTED");
    MQTT_DEBUG_PRINTLN("====================================");
    MQTT_DEBUG_PRINTLN("====================================");
    this->_is_connecting = false;

    if (_config.publishOnly == true) {
       // delete _user_hook_prepare_subscribe;
       _user_hook_prepare_subscribe = NULL;
    }

    if (_user_hook_prepare_subscribe != NULL)
    {
        MQTT_DEBUG_PRINTLN("CALLING HOOK SUBSCRIBING..");
        _user_hook_prepare_subscribe(_subscribe_object);
        MQTT_DEBUG_PRINTLN("CHECK IF __SUBSCRIBING... ->");

        _subscribe_object->add_topic(_config.topicSub);
        MQTT_DEBUG_PRINTLN("++TRY SUBSCRIBING ++");
            if (this->_client->subscribe(*_subscribe_object)) {
                _subscription_counter++;
                MQTT_DEBUG_PRINT("__SUBSCRIBED TO ");
                MQTT_DEBUG_PRINTLN(_config.topicSub);
            }
            else {
                // goto loop and recheck connectiviy
                return;
            }
    }
    else
    {
        MQTT_DEBUG_PRINTLN("__ PUBLISH ONLY MODE");
    }

    if (_config.enableLastWill) {
        _clear_last_will();
    }

    _do_publish(true);
}

bool MqttConnector::connected() {
    return this->_client->connected();
}


/*
*******************************
************* HOOKS  **********
*******************************
*/

void MqttConnector::on_prepare_configuration(cmmc_config_t func)
{
    _user_hook_config = func;
}

void MqttConnector::on_after_prepare_configuration(cmmc_after_config_t func)
{
    _user_hook_after_config = func;
}

void MqttConnector::on_connecting(connecting_hook_t cb) {
    _user_hook_connecting = cb;
}

void MqttConnector::on_prepare_data(prepare_data_hook_t func, unsigned long publish_interval)
{
    _user_hook_prepare_data = func;
    _publish_interval = publish_interval;
    _timer_set(&publish_timer, publish_interval);
}

void MqttConnector::on_after_prepare_data(after_prepare_data_hook_t func)
{
    _user_hook_after_prepare_data = func;
}

void MqttConnector::on_prepare_subscribe(prepare_subscribe_hook_t func)
{
    _user_hook_prepare_subscribe = func;
}

void MqttConnector::set_publish_data_hook(publish_data_hook_t func)
{
    _user_hook_publish_data = func;
}

MqttConnector::~MqttConnector()
{
    delete _config.connOpts;
    delete _config.client;
    delete _subscribe_object;
    _config.connOpts = NULL;
    _config.client = NULL;
    _subscribe_object = NULL;
}