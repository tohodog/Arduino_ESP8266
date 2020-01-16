//-------------------------基础框架------------------------------
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>

//WIFI信息
String ssid = "ICBM";          //WiFi名
String password = "Androids";  //WiFi密码

//-------------------------DigitalTube------------------------------
//硬件连接说明：
//MAX7219 --- ESP8266
//  VCC   --- 3V(3.3V)
//  GND   --- G (GND)
//  DIN   --- D7(GPIO13)
//  CS    --- D1(GPIO5)
//  CLK   --- D5(GPIO14)
const int pinTube = 5;
const int scanLimit = 7;

//-------------------------DHT11------------------------------
//硬件连接说明：
//      VCC: 5V or 3V
//      GND: GND
//      DATA: DX  第几个D插针

#include <SimpleDHT.h>

const int pinDHT11 = D4;
SimpleDHT11 dht11(pinDHT11);
// read without samples.
byte temperature = 99;
byte humidity = 0;

//-------------------------网络时间------------------------------

#include <NTPClient.h>
#include <WiFiUdp.h>

const char* ntpServer = "ntp1.aliyun.com";
const int gmtOffset_sec = 8 * 3600; //这里采用UTC计时，中国为东八区，就是 8*60*60
const int daylightOffset_sec = 8 * 3600; //同上

WiFiUDP ntpUDP;
// You can specify the time server pool and the offset (in seconds, can be
// changed later with setTimeOffset() ). Additionaly you can specify the
// update interval (in milliseconds, can be changed using setUpdateInterval() ).
NTPClient timeClient(ntpUDP, ntpServer, gmtOffset_sec, 60000);


//-------------------------开关------------------------------
const int pinSwitch = D0;
byte isSwitchOpen = 0;

//-------------------------logic------------------------------

void setup()
{
  //打开串口
  Serial.begin(9600);
  while (!Serial)
    continue;

  Serial.println("\nQsong project for esp8266, version v1.0");

  //init digitalTube
  SPI.begin();
  pinMode(pinTube, OUTPUT);
  digitalWrite(pinTube, LOW);
  sendTubeCommand(12, 1);         //Shutdown,open
  sendTubeCommand(15, 0);         //DisplayTest,no
  sendTubeCommand(10, 8);        //Intensity,15(max)
  sendTubeCommand(11, scanLimit); //ScanLimit,8-1=7
  sendTubeCommand(9, 255);        //DecodeMode,Code B decode for digits 7-0
  digitalWrite(pinTube, HIGH);
  initDisplay(0);
  Serial.println("DigitalTube Ready");

  //init switch
  pinMode(pinSwitch, OUTPUT);
  switchPin(pinSwitch, isSwitchOpen);

  getDHT11();
  connWifi();
}

void loop() {

  //  displayNumber(follower -= 99);
  //  return;

  if (WiFi.status() == WL_CONNECTED) {
    for (int i = 0; i < 4; i++) {
      getTime();
      delayAndHandleTask(1000);
    }
    getDHT11();
    delayAndHandleTask(1000);
    runBili();
    delayAndHandleTask(1000);

  } else {
    Serial.println("[WiFi] Waiting to reconnect...");
    //    errorCode(0x1);
    initDisplay(millis() / 100);
    delay(100);
  }

}

void connWifi() {
  Serial.println("ssid:" + ssid + " password:" + password);
  Serial.print("Connecting WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int i = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
    initDisplay(i++);

    //30秒沒连上网打开智能配网模式
    if (i % 300 == 0) {
      smartConfig(30);
      WiFi.begin(ssid, password);
    }
  }
  Serial.print("\nWIFI connected, IP address: ");
  Serial.println(WiFi.localIP());
}


bool getDHT11() {
  // start working...
  int err = SimpleDHTErrSuccess;
  if ((err = dht11.read(&temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
    Serial.print("Read DHT11 failed, err="); Serial.println(err);
    displayDHT11(temperature, humidity);
    return false;
  }

  Serial.printf("DHT11 Read OK: %d *C, %d H\n", (int) temperature, (int) humidity);
  displayDHT11(temperature, humidity);
  return true;
}

bool getTime() {

  timeClient.update();
  Serial.println("NTPTime:" + timeClient.getFormattedTime());

  int hours = timeClient. getHours();
  int minutes = timeClient. getMinutes();
  int seconds = timeClient. getSeconds();

  sendTubeCommand(8, hours / 10);
  sendTubeCommand(7, hours % 10);
  sendTubeCommand(6, 0xa);
  sendTubeCommand(5, minutes / 10);
  sendTubeCommand(4, minutes % 10);
  sendTubeCommand(3, 0xa);
  sendTubeCommand(2, seconds / 10);
  sendTubeCommand(1, seconds % 10);

  return true;
}


void switchPin(int pinSwitch, byte low) {
  digitalWrite(pinSwitch, low ? LOW : HIGH);
}

int updataMillis = -99999999; //上次上报数据时间
//空闲时间检测是否有消息和任务
bool delayAndHandleTask(int timeout) {
  //  Serial.print("delayAndHandleTask..." + timeout);
  //  Serial.println("");
  timeout = timeout + millis();
  while (millis() < timeout) {
    checkSerialIO();
    checkTCPIO();

    //一分钟自动上报设备信息
    if (millis() - updataMillis > 60000) {
      if (uploadData()) {
        updataMillis = millis();
      }
    }

    delay(5);
  }
  //  Serial.println("delayAndHandleTask done");
  return false;
}

//检测串口消息
void checkSerialIO() {
  if (Serial.available()) //if number of bytes (characters) available for reading from serial port
  {
    char c = Serial.read();
    Serial.print("I received:"); //print I received
    Serial.println(c); //send what you read
    if (c == 's')smartConfig(30);
  }
}

//-------------------------TCP START------------------------------
WiFiClient client;
//设备唯一标识,后台提前入库
const char* DEVICE_ID = "S5FE62HHYDBI";
String host = "api.reol.top";
const uint16_t port = 8899;
int MSG_ID = 0;
//检测TCP连接消息
bool checkTCPIO() {
  //检测建立连接
  if (!client.connected()) {
    if (!client.connect(host, port)) {//5s timeout
      Serial.println("TCP Connection Failed->" + host + ":" + port);
      return false;
    } else {
      Serial.println("TCP Connection OK->" + host + ":" + port);
      //建立连接发送设备id,授权
      String id = DEVICE_ID;
      uploadDeviceId();
    }
  } else {
    //tcp连接建立
    listenTCP();
  }
  return true;
}
//监听TCP消息
void listenTCP() {
  //有消息
  if (client.available() > 0) {
    //默认超时1秒
    String json = client.readStringUntil('\n');
    Serial.println("TCP Read->" + json);

    const size_t capacity = JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + 70;
    DynamicJsonDocument doc(capacity);
    deserializeJson(doc, json);
    int command = doc["command"];
    int msg_id = doc["msg_id"];

    if (command == 0) {//上报设备id/心跳包
      uploadDeviceId(msg_id);
    } else if (command == 1) { //立即上报信息
      uploadData(msg_id);
    } else if (command == 2) { //控制开关
      isSwitchOpen = doc["data"];
      switchPin(pinSwitch, isSwitchOpen);
      uploadData(msg_id);
    }

  }
}
//上报设备所有信息,后期复杂可根据命令上报对应消息,
bool uploadData() {
  return uploadData(MSG_ID++);
}
//消息id是确定回复对应的请求
bool uploadData(int msg_id) {
  StaticJsonDocument<200> doc;
  doc["msg_id"] = msg_id;
  doc["type"] = 1;
  doc["switch"] = isSwitchOpen;
  doc["temp"] = temperature;
  doc["humi"] = humidity;
  String output;
  serializeJson(doc, output);
  return sendTCP(output);
}
//上报设备id/心跳包回复
bool uploadDeviceId() {
  return uploadDeviceId(MSG_ID++);
}
bool uploadDeviceId(int msg_id) {
  StaticJsonDocument<200> doc;
  doc["msg_id"] = msg_id;
  doc["type"] = 0;
  doc["device_id"] = DEVICE_ID;
  String output;
  serializeJson(doc, output);
  return sendTCP(output);
}
//发送TCP消息
bool sendTCP(String msg) {
  if (client.connected()) {
    Serial.println("TCP Send OK->" + msg);
    client.println(msg);
    return true;
  } else {
    Serial.println("TCP Send Fail->" + msg);
    return false;
  }
}
//-------------------------TCP END------------------------------


void displayNumber(int number) //display number in the middle
{
  if (number < -9999999 || number > 99999999)
    return;
  int x = 1;//数字有几位
  int tmp = number;
  for (x = 1; tmp /= 10; x++);
  tmp = number;

  int sI = 4 + x / 2;
  int eI = sI - x + 1;
  for (int i = 1; i < 9; i++) {
    if (i < eI || i > sI) {
      if (number < 0 && (i == (sI + 1)))
        sendTubeCommand(i, 0xa);
      else
        sendTubeCommand(i, 0xf);
    } else {
      int character = tmp % 10;
      if (character < 0)character = -character;
      sendTubeCommand(i, character);
      tmp /= 10;
    }
  }
}

void displayDHT11(int temperature, int humidity)
{
  sendTubeCommand(8, 0xf);
  sendTubeCommand(7, 0xf);
  sendTubeCommand(6, 0xf);

  sendTubeCommand(5, temperature / 10);
  sendTubeCommand(4, temperature % 10);

  sendTubeCommand(3, 0xa);
  sendTubeCommand(2, humidity / 10);
  sendTubeCommand(1, humidity % 10);
}

void initDisplay(int pro)
{
  pro = pro % 8;
  for (int i = 1; i < 9; i++) {
    sendTubeCommand(8, pro > 3 & pro < 7 ? 0xa : 0xf);
    sendTubeCommand(7, pro > 2 & pro < 6 ? 0xa : 0xf);
    sendTubeCommand(6, pro > 1 & pro < 5 ? 0xa : 0xf);
    sendTubeCommand(5, pro > 0 & pro < 4 ? 0xa : 0xf);
    sendTubeCommand(4, pro > 0 & pro < 4 ? 0xa : 0xf);
    sendTubeCommand(3, pro > 1 & pro < 5 ? 0xa : 0xf);
    sendTubeCommand(2, pro > 2 & pro < 6 ? 0xa : 0xf);
    sendTubeCommand(1, pro > 3 & pro < 7 ? 0xa : 0xf);
  }
}

void errorCode(byte errorcode)
{
  sendTubeCommand(8, 0xa);
  sendTubeCommand(7, 0xa);
  sendTubeCommand(6, 0xa);
  sendTubeCommand(5, 0xb);
  sendTubeCommand(4, errorcode);
  sendTubeCommand(3, 0xa);
  sendTubeCommand(2, 0xa);
  sendTubeCommand(1, 0xa);
}

void sendTubeCommand(int command, int value)
{
  digitalWrite(pinTube, LOW);
  SPI.transfer(command);
  SPI.transfer(value);
  digitalWrite(pinTube, HIGH);
}

//智能配网 timeout-秒
void smartConfig(int timeout)
{
  WiFi.mode(WIFI_STA);
  Serial.print("\r\nWait for Smartconfig");
  //  delay(2000);// 等待配网
  WiFi.beginSmartConfig();
  timeout = timeout * 1000;
  while (timeout > 0)
  {
    Serial.print(".");
    initDisplay(timeout / 100);
    delay(100);
    if (WiFi.smartConfigDone())
    {
      Serial.println("");
      Serial.println("SmartConfig Success->");
      ssid = WiFi.SSID().c_str();
      password = WiFi.psk().c_str();
      Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str());
      Serial.printf("PSW:%s\r\n", WiFi.psk().c_str());
      WiFi.setAutoConnect(true);  // 设置自动连接
      break;
    }
    timeout -= 100;
  }
  WiFi.stopSmartConfig();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\nWIFI connected, IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nSmartConfig Fail->Timeout!");
  }

}


//------------------------BILIBLI-------------------------------

String biliuid = "423895";         //bilibili UID
const unsigned long HTTP_TIMEOUT = 5000;
HTTPClient http;
String response;
int follower = 0;

void runBili() {
  if (getJson()) {
    if (parseJson(response)) {
      displayNumber(follower);
    }
  }
}

bool getJson()
{
  String api = "http://api.bilibili.com/x/relation/stat?vmid=" + biliuid;
  Serial.println("Request:" + api);
  bool r = false;
  http.setTimeout(HTTP_TIMEOUT);
  http.begin(api);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    response = http.getString();
    Serial.println(response);
    r = true;
  } else {
    Serial.printf("[HTTP] GET JSON failed, error: %s\n", http.errorToString(httpCode).c_str());
    errorCode(0x2);
    r = false;
  }
  http.end();
  return r;
}

bool parseJson(String json)
{
  const size_t capacity = JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + 70;
  DynamicJsonDocument doc(capacity);
  deserializeJson(doc, json);

  int code = doc["code"];
  const char *message = doc["message"];

  if (code != 0) {
    Serial.print("[API]Code:");
    Serial.print(code);
    Serial.print(" Message:");
    Serial.println(message);
    errorCode(0x3);
    return false;
  }

  JsonObject data = doc["data"];
  unsigned long data_mid = data["mid"];
  int data_follower = data["follower"];
  if (data_mid == 0) {
    Serial.println("[JSON] FORMAT ERROR");
    errorCode(0x4);
    return false;
  }
  Serial.print("UID: ");
  Serial.print(data_mid);
  Serial.print(" follower: ");
  Serial.println(data_follower);

  follower = data_follower;
  return true;
}