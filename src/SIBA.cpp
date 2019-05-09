/*
 * SIBA.cpp
 * Created by GyoJun Ahn @ DCU ICS Lab. on 05/08/2019
 *  
 */

#include "SIBA.h"
#include <Arduino.h>
#include "includes/PubSubClient/PubSubClient.h"
#include "includes/ArduinoJson/ArduinoJson.h"
#include <ESP8266WiFi.h>

#if defined(ESP8266) || defined(ESP32)
#include <functional>
#endif

//initialize static member
SIBA SIBA::context;
size_t SIBA::action_cnt = 0;
sb_event SIBA::action_store[EVENT_COUNT] = {0};

SIBA::SIBA()
{
  //production 이냐 develop 이냐에 따라서 mqtt server의 주소가 달라질 것, 추후에 수정해야
  this->mqtt_server = MQTT_SERVER;
  this->mqtt_port = 1883;

  this->espClient = WiFiClient();
  this->client = PubSubClient(espClient);
}

size_t SIBA::init(const char *ssid, const char *pwd, const char *dev_type)
{
  //static function내에서 멤버 함수를 실행 할 수 없으므로 트릭 사용
  context = *this;

  //instance field value init
  this->ssid = const_cast<char *>(ssid);
  this->pwd = const_cast<char *>(pwd);
  this->dev_type = const_cast<char *>(dev_type);

  //basic setup function call
  this->init_wifi(this->ssid, this->pwd);
  this->client.setServer(this->mqtt_server, this->mqtt_port);

#if defined(ESP8266) || defined(ESP32)
  std::function<void(char *, uint8_t *, unsigned int)> callback = SIBA::mqtt_callback;
#else
  void (*callback)(char *, uint8_t *, unsigned int);
  callback = SIBA::mqtt_callback;
#endif

  this->client.setCallback(callback);

  return 1;
}

void SIBA::init_wifi(char *ssid, char *pwd)
{

  delay(10);
  // We start by connecting to a WiFi network
  Serial.print(F("\nConnecting to "));
  Serial.println(ssid);

  WiFi.begin(ssid, pwd);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(F("."));
  }

  randomSeed(micros());

  //this->cur_ip = const_cast<char*>(WiFi.localIP().toString().c_str());
  //this->mac_address = const_cast<char*>(WiFi.macAddress().c_str());

  this->cur_ip = WiFi.localIP().toString();
  this->mac_address = WiFi.macAddress();

  Serial.println("");
  Serial.println(F("WiFi connected"));
  Serial.print(F("IP address: "));
  Serial.println(this->cur_ip);
  Serial.print(F("MAC address: "));
  Serial.println(this->mac_address);
}

size_t SIBA::add_event(size_t code, SB_ACTION)
{
  size_t ret = 0;
  if (ret = action_cnt != EVENT_COUNT - 1)
  { //액션 스토어의 범위 내라면
    action_store[action_cnt++] = {code, sb_action};
  }
  return ret;
}

void (*SIBA::grep_event(size_t code))()
{
  //sequential search
  size_t temp_index = action_cnt;
  SB_ACTION = NULL;

  while (temp_index--)
  {
    if (action_store[temp_index].sb_code == code)
    {
      sb_action = action_store[temp_index].sb_action;
      break;
    }
  }

  return sb_action;
}

size_t SIBA::exec_event(SB_ACTION)
{
  size_t ret = 0;

  if (ret = sb_action != NULL)
  {
    sb_action(); //액션 수행
  }
  else
  {
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO)
    Serial.println(F("action is empty"));
#endif
  }

  return ret;
}

size_t SIBA::pub_result(size_t action_res)
{
  if (action_res)
  {
    sb_keypair sets[] = {{"status", "true"}};

    this->publish_topic(DEV_CTRL_END, sets, sizeof(sets) / sizeof(sets[0]));
  }
}

void SIBA::regist_dev()
{
  sb_keypair sets[] = {
      {"dev_mac", this->mac_address},
      {"cur_ip", this->cur_ip},
      {"dev_type", this->dev_type}};

#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO)
  Serial.println(F("register information send"));
#endif

  this->publish_topic(DEV_REG, sets, sizeof(sets) / sizeof(sets[0]));
}

void SIBA::subscribe_topic(char* topic)
{
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO)
  Serial.println(F("subscribe topic."));
#endif
  this->client.subscribe(topic);
}

void SIBA::publish_topic(char *topic, sb_keypair sets[], uint16_t len)
{
  StaticJsonDocument<256> doc;
  char buffer[256];

  while (len--)
  {
    doc[sets[len].key] = sets[len].value;
  }

  size_t n = serializeJson(doc, buffer);
  Serial.println(F("publish topic"));
  this->client.publish(topic, buffer, n);
}

void SIBA::mqtt_reconnect()
{
  // Loop until we're reconnected
  while (!this->client.connected())
  {
    Serial.print(F("Attempting MQTT connection..."));

    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);

    // Attempt to connect
    if (this->client.connect(clientId.c_str()))
    {
      Serial.println(F("connected"));

      // Once connected, publish an announcement...
      this->regist_dev();
      // ... and resubscribe


      String buf = String(DEV_CTRL) + "/" + this->mac_address;
      char* topic = const_cast<char *>(buf.c_str());
      this->subscribe_topic(topic);
    }
    else
    {
      Serial.print(F("failed, rc="));
      Serial.print(client.state());
      Serial.println(F(" try again in 5 seconds"));

      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void SIBA::mqtt_callback(char *topic, uint8_t *payload, unsigned int length)
{
  String msg = "";

  Serial.print(F("Message arrived ["));
  Serial.print(topic);
  Serial.print(F("] "));

  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  for (int i = 0; i < length; i++)
  {
    msg += (char)payload[i];
  }

  //받아온 json문자열 파싱, 파싱 실패시 함수 수행 중단
  StaticJsonDocument<256> doc;
  auto error = deserializeJson(doc, msg);
  if (error)
  {
    Serial.print(F("deserializeJson() failed with code "));
    Serial.println(error.c_str());
    return;
  }

  Serial.println(msg);

  JsonArray cmd_list = doc[F("cmd")].as<JsonArray>();
  for (int i = cmd_list.size() - 1; i >= 0; i--)
  {
    JsonObject elem = cmd_list.getElement(i).as<JsonObject>();

    int code = elem[F("cmd_code")];

    SB_ACTION = context.grep_event(code);
    size_t action_result = context.exec_event(sb_action);
    context.pub_result(action_result);
  }
}

void SIBA::verify_connection()
{
  if (!this->client.connected())
  {
    this->mqtt_reconnect();
  }
  this->client.loop();
}