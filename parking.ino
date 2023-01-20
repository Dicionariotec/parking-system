#include <SocketIOclient.h>
#include <Servo.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Informacoes do WIFI
const char* ssid = "Buffalo-G-12C8";
const char* password = "3b6hcpdfm4wsp";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

IPAddress local_IP(192, 168, 1, 20);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

const char *mqtt_broker = "raspberrypi.local"; // Enter your WiFi or Ethernet IP
const char *topic = "park/situation";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// Sensor pins
#define ENTRANCE_SENSOR_PIN D0
#define EXIT_SENSOR_PIN D5

// Park slot pins
#define PARK_SLOT1_SENSOR_PIN D2
#define PARK_SLOT2_SENSOR_PIN D3

// Motor pins
#define ENTRANCE_SERVO_PIN D1
#define EXIT_SERVO_PIN D6

// Sensor values
int entranceDetectorValue = 0;
unsigned long entraceOpenStartTime;
const unsigned long entranceOpenPeriod = 5000; 

int exitDetectorValue = 0;
unsigned long exitOpenStartTime;
const unsigned long exitOpenPeriod = 5000;

int parkSlot1Detector = 0;
int parkSlot2Detector = 0;

bool isSlot1Occupied = false;
unsigned long slot1ParkStartTime;
const unsigned long slot1CheckPeriod = 10000; 

bool isSlot2Occupied = false;
unsigned long slot2ParkStartTime;
const unsigned long slot2CheckPeriod = 10000; 

bool isEntranceOpen = false;
bool isExitOpen = false;

// Motors
Servo entranceMotor;
Servo exitMotor;

class ParkData {
  public:
    String parkNumber;
    bool parkSituation;
    String datetime;
    ParkData(String pn, bool ps, String d);
};

ParkData::ParkData(String pn, bool ps, String d) {
  parkNumber = pn;
  parkSituation = ps;
  datetime = d;
};

void setupWifi() {
  delay(100);

  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  randomSeed(micros());

  // init web socket

  Serial.println(WiFi.localIP());
}

void sendParkSituation(ParkData parkData) {

  StaticJsonDocument<256> staticJsonDocument;
  staticJsonDocument["slot"] = parkData.parkNumber;
  staticJsonDocument["vacancy"] = parkData.parkSituation;
  staticJsonDocument["datetime"] = parkData.datetime;

  char requestBody[128];
  serializeJson(staticJsonDocument, requestBody);

  Serial.println(requestBody);
  client.publish(topic, requestBody);

  delay(1000);
}

void callback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  Serial.print("Message:");

  for (int i = 0; i < length; i++) {
    Serial.print((char) payload[i]);
  }

  Serial.println();
  Serial.println(" - - - - - - - - - - - -");
}

void connectMqtt() {
  while (!client.connected()) {
    String client_id = "esp8266-client-";
    client_id += String(WiFi.macAddress());

    Serial.printf("The client %s connects to mosquitto mqtt broker\n", client_id.c_str());

    if (client.connect(client_id.c_str())) {
      Serial.println("Public emqx mqtt broker connected");
    } else {
      Serial.print("failed with state ");
      Serial.print(client.state());
      delay(1000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Data transfer
  setupWifi();

  timeClient.begin();
  timeClient.setTimeOffset(32400);
  
  Serial.println("Starting...");

  entranceMotor.attach(ENTRANCE_SERVO_PIN);
  exitMotor.attach(EXIT_SERVO_PIN);

  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(callback);

  connectMqtt();

  pinMode(ENTRANCE_SENSOR_PIN, INPUT);
  pinMode(EXIT_SENSOR_PIN, INPUT);
  pinMode(PARK_SLOT1_SENSOR_PIN, INPUT);
  pinMode(PARK_SLOT2_SENSOR_PIN, INPUT);

  ParkData parkData("Connection", true, "");
  sendParkSituation(parkData);
}

void loop() {
  timeClient.update();
  connectMqtt();
  client.loop();

  unsigned long epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime ((time_t *)&epochTime);
  String formattedTime = timeClient.getFormattedTime();
  int monthDay = ptm->tm_mday;
  int currentMonth = ptm->tm_mon+1;
  int currentYear = ptm->tm_year+1900;
  String currentDate = String(currentYear) + "/" + String(currentMonth) + "/" + String(monthDay);
  String fullDatetime = currentDate + " " + formattedTime;

  entranceDetectorValue = !digitalRead(ENTRANCE_SENSOR_PIN);
  exitDetectorValue = !digitalRead(EXIT_SENSOR_PIN);

  unsigned long currentTime = millis();

  parkSlot1Detector = !digitalRead(PARK_SLOT1_SENSOR_PIN);
  parkSlot2Detector = !digitalRead(PARK_SLOT2_SENSOR_PIN);
  
  // Entrance
  if (entranceDetectorValue == 1 && !isEntranceOpen) {
    entranceMotor.write(120);
    entraceOpenStartTime = currentTime;
    isEntranceOpen = true;
    ParkData parkData("Entrance", isEntranceOpen, fullDatetime);
    sendParkSituation(parkData);
  }

  if (entranceDetectorValue == 1 && isEntranceOpen && (currentTime - entraceOpenStartTime >= entranceOpenPeriod)) {
    entraceOpenStartTime = currentTime;
    Serial.println("Keep Entrance Opened");
  }

  if (entranceDetectorValue == 0 && isEntranceOpen && (currentTime - entraceOpenStartTime >= entranceOpenPeriod)) {
    entranceMotor.write(0);
    Serial.println("Close Entrance");
    isEntranceOpen = false;
    ParkData parkData("Entrance", isEntranceOpen, fullDatetime);
    sendParkSituation(parkData);
  }

  // Exit 
  if (exitDetectorValue == 1 && !isExitOpen) {
    exitMotor.write(120);
    exitOpenStartTime = currentTime;
    isExitOpen = true;
    ParkData parkData("Exit", isExitOpen, fullDatetime);
    sendParkSituation(parkData);
  }

  if (exitDetectorValue == 1 && isExitOpen && (currentTime - exitOpenStartTime >= exitOpenPeriod)) {
    exitOpenStartTime = currentTime;
    Serial.println("Keep Exit Opened");
  }

  if (exitDetectorValue == 0 && isExitOpen && (currentTime - exitOpenStartTime >= exitOpenPeriod)) {
    exitMotor.write(0);
    isExitOpen = false;
    ParkData parkData("Exit", isExitOpen, fullDatetime);
    sendParkSituation(parkData);
  }

  // Park Slot 1  
  if (parkSlot1Detector == 1 && !isSlot1Occupied) {
    slot1ParkStartTime = currentTime;
    isSlot1Occupied = true;
    ParkData parkData("Slot 1", isSlot1Occupied, fullDatetime);
    sendParkSituation(parkData);
  }

  if (parkSlot1Detector == 0 && isSlot1Occupied && (currentTime - slot1ParkStartTime >= slot1CheckPeriod)) {
    slot1ParkStartTime = currentTime;
    isSlot1Occupied = false;
    ParkData parkData("Slot 1", isSlot1Occupied, fullDatetime);
    sendParkSituation(parkData);
  }

  // Park Slot 2
  if (parkSlot2Detector == 1 && !isSlot2Occupied) {
    slot2ParkStartTime = currentTime;
    isSlot2Occupied = true;
    ParkData parkData("Slot 2", isSlot2Occupied, fullDatetime);
    sendParkSituation(parkData);
  }

  if (parkSlot2Detector == 0 && isSlot2Occupied && (currentTime - slot2ParkStartTime >= slot2CheckPeriod)) {
    slot2ParkStartTime = currentTime;
    isSlot2Occupied = false;
    ParkData parkData("Slot 2", isSlot2Occupied, fullDatetime);
    sendParkSituation(parkData);    
  }
}