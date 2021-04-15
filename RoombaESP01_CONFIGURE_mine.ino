#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <SimpleTimer.h>
#include <Roomba.h>

#define ON 1
#define OFF 0

//USER CONFIGURED SECTION START//
String device_hostname = "Roomba886"; // every client must have a different name
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "MQTT_SERVER_IP";
const int mqtt_port = 1883; //default is 1883
const char *mqtt_user = "MQTT_USERNAME";
const char *mqtt_pass = "MQTT_PASSWORD";
const char *OTApassword = "OTA_PASSWORD";
const int OTAport = 8266; //default is 8266
//USER CONFIGURED SECTION END//

//Define MQTT Topics
String TopicCheckIn = "roomba/" + device_hostname + "/checkIn";
String TopicCommands = "roomba/" + device_hostname + "/commands";
String TopicBattery = "roomba/" + device_hostname + "/battery";
String TopicCharging = "roomba/" + device_hostname + "/charging";
String TopicStatus = "roomba/" + device_hostname + "/status";

WiFiClient espClient;
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
PubSubClient client(espClient);
SimpleTimer timer;
Roomba roomba(&Serial, Roomba::Baud115200);

// Variables
bool boot = true;
long battery_Current_mAh = 0;
long battery_Voltage = 0;
long battery_Total_mAh = 0;
long battery_percent = 0;
char battery_percent_send[50];
char battery_Current_mAh_send[50];
uint8_t tempBuf[10];

// add to support wakeup
bool toggle = true;
int BRC_PIN = 2; //D4 on WeMos D1 Mini
bool debrisLED;
bool spotLED;
bool dockLED;
bool warningLED;
byte color;
byte intensity;

//Functions
void setup_wifi()
{
  WiFi.hostname(device_hostname.c_str());
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
  }
}

void setup_OTA() {
  ArduinoOTA.setPort(OTAport);
  ArduinoOTA.setHostname(device_hostname.c_str());
  ArduinoOTA.setPassword((const char *)OTApassword);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void setup_httpUpdater ()
{
  MDNS.begin(device_hostname.c_str());
  httpUpdater.setup(&httpServer);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", device_hostname.c_str());
}

void reconnect()
{
  // Loop until we're reconnected
  int retries = 0;
  while (!client.connected())
  {
    if(retries < 50)
    {
      // Attempt to connect
      if (client.connect(device_hostname.c_str(), mqtt_user, mqtt_pass, TopicCheckIn.c_str(), 0, 0, "Dead Somewhere"))
      {
        // Once connected, publish an announcement...
        if(boot == false)
        {
          client.publish(TopicCheckIn.c_str(), "Reconnected");
        }
        if(boot == true)
        {
          client.publish(TopicCheckIn.c_str(), "Rebooted");
          boot = false;
        }
        // ... and resubscribe
        client.subscribe(TopicCommands.c_str());
      }
      else
      {
        retries++;
        // Wait 5 seconds before retrying
        delay(5000);
      }
    }
    if(retries >= 50)
    {
    ESP.restart();
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length)
{
  String newTopic = topic;
  payload[length] = '\0';
  String newPayload = String((char *)payload);
  if (newTopic == TopicCommands.c_str())
  {
    if (newPayload == "wakeUp")
    {
      wakeUp();
    }
    if (newPayload == "start")
    {
      startCleaning();
    }
    if (newPayload == "stop")
    {
      stopCleaning();
    }
  }
}

void setWarningLED(bool enable)
{
  warningLED = enable;
  Serial.write(139);
  Serial.write((debrisLED ? 1 : 0) + (spotLED ? 2 : 0) + (dockLED ? 4 : 0) + (warningLED ? 8 : 0));
  Serial.write((byte)color);
  Serial.write((byte)intensity);
}

void wakeUp ()
{
  setWarningLED(ON);
  digitalWrite(BRC_PIN, HIGH);
  delay(1000);
  digitalWrite(BRC_PIN, LOW);
  delay(1000);
  digitalWrite(BRC_PIN, HIGH);
  delay(1000);
  digitalWrite(BRC_PIN, LOW);
  delay(1000);
  client.publish(TopicStatus.c_str(), "Awake");
}

void startUp() // was start Cleaning
{
  roomba.start();
  delay(50);
  roomba.safeMode();
  delay(50);
  roomba.cover();
}

void startCleaning()
{
  wakeUp();
  startUp(); // for some reason i need to send twice
  startUp();
  client.publish(TopicStatus.c_str(), "Cleaning");
}

void stopCleaning()
{
  roomba.start();
  delay(50);
  roomba.safeMode();
  delay(50);
  roomba.dock();
  client.publish(TopicStatus.c_str(), "Returning");
}

void sendInfoRoomba()
{
  roomba.start();
  roomba.getSensors(Roomba::SensorChargingState, tempBuf, 1); // 21
  battery_Voltage = tempBuf[0];
  delay(50);
  roomba.getSensors(Roomba::SensorBatteryCharge, tempBuf, 2); // 25
  battery_Current_mAh = tempBuf[1]+256*tempBuf[0];
  delay(50);
  roomba.getSensors(Roomba::SensorBatteryCapacity, tempBuf, 2); // 26
  battery_Total_mAh = tempBuf[1]+256*tempBuf[0];
  if(battery_Total_mAh != 0)
  {
    int nBatPcent = 100*battery_Current_mAh/battery_Total_mAh;
    if (nBatPcent >= 0 && nBatPcent <= 100)
    {
      String temp_str2 = String(nBatPcent);
      temp_str2.toCharArray(battery_percent_send, temp_str2.length() + 1); //packaging up the data to publish to mqtt
      client.publish(TopicBattery.c_str(), battery_percent_send);
    }
  }
  if(battery_Total_mAh == 0)
  {
    client.publish(TopicBattery.c_str(), "NO DATA");
  }
  if( battery_Voltage >= 0 && battery_Voltage <= 5)
  {
    String temp_str = String(battery_Voltage);
    temp_str.toCharArray(battery_Current_mAh_send, temp_str.length() + 1); //packaging up the data to publish to mqtt
    client.publish(TopicCharging.c_str(), battery_Current_mAh_send);
  }
}

void stayAwakeLow()
{
  digitalWrite(BRC_PIN, LOW);
  timer.setTimeout(1000, stayAwakeHigh);
}

void stayAwakeHigh()
{
  digitalWrite(BRC_PIN, HIGH);
}

void setup()
{
  pinMode(BRC_PIN, OUTPUT);
  digitalWrite(BRC_PIN, HIGH);
  Serial.begin(115200);
  roomba.baud(Roomba::Baud115200);
  delay(50);
  WiFi.mode(WIFI_STA);
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  timer.setInterval(5000, sendInfoRoomba);
  timer.setInterval(50000, stayAwakeLow);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  setup_OTA();
  setup_httpUpdater();
  Serial.println("Ready");
}

void loop()
{
  ArduinoOTA.handle();
  httpServer.handleClient();
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();
  timer.run();
}
