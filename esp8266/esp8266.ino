
//------------------------BILIBLI-------------------------------

//硬件连接说明：
//MAX7219 --- ESP8266
//  VCC   --- 3V(3.3V)
//  GND   --- G (GND)
//  DIN   --- D7(GPIO13)
//  CS    --- D1(GPIO5)
//  CLK   --- D5(GPIO14)

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>

//WIFI信息
String ssid = "ICBM";          //WiFi名
String password = "Androids";  //WiFi密码
String biliuid = "423895";         //bilibili UID


const unsigned long HTTP_TIMEOUT = 5000;
HTTPClient http;
String response;
int follower = 0;
const int pinTube = 5;
const int scanLimit = 7;

//-------------------------DHT11------------------------------
//硬件连接说明：
//      VCC: 5V or 3V
//      GND: GND
//      DATA: DX  第几个D插针

#include <SimpleDHT.h>

const int pinSwitch = D0;
const int pinDHT11 = D4;
SimpleDHT11 dht11(pinDHT11);


//-------------------------网络时间------------------------------

#include <NTPClient.h>
#include <WiFiUdp.h>

const char* ntpServer = "ntp1.aliyun.com";
const long  gmtOffset_sec = 8 * 3600; //这里采用UTC计时，中国为东八区，就是 8*60*60
const int   daylightOffset_sec = 8 * 3600; //同上

WiFiUDP ntpUDP;
// You can specify the time server pool and the offset (in seconds, can be
// changed later with setTimeOffset() ). Additionaly you can specify the
// update interval (in milliseconds, can be changed using setUpdateInterval() ).
NTPClient timeClient(ntpUDP, ntpServer, gmtOffset_sec, 60000);


//-------------------------logic------------------------------

void setup()
{
  //打开串口
  Serial.begin(9600);
  while (!Serial)
    continue;

  Serial.println("\nbilibili fans monitor, version v1.2");

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
  Serial.println("LED Ready");

  //init DHT11
  pinMode(pinSwitch, OUTPUT);
  digitalWrite(pinSwitch, HIGH);
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

    if (getJson()) {
      if (parseJson(response)) {
        displayNumber(follower);
      }
    }
  } else {
    Serial.println("[WiFi] Waiting to reconnect...");
    errorCode(0x1);
  }

  delayAndHandleTask(1000);

  for (int i = 0; i < 1; i++) {
    getDHT11();
    delayAndHandleTask(1000);
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
  Serial.println("Sample DHT11...");

  // read without samples.
  byte temperature = 0;
  byte humidity = 0;
  int err = SimpleDHTErrSuccess;

  if ((err = dht11.read(&temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
    Serial.print("Read DHT11 failed, err="); Serial.println(err);
    displayDHT11(temperature, humidity);
    return false;
  }

  Serial.printf("DHT11 Read OK: %d *C, %d H\n", (int) temperature, (int) humidity);
  displayDHT11(temperature, humidity);
  digitalWrite(pinSwitch, humidity > 60 ? LOW : HIGH);

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

//空闲时间检测是否有消息任务
bool delayAndHandleTask(int timeout) {
  Serial.print("delayAndHandleTask...");
  Serial.print(timeout);
  Serial.println("");
  timeout = timeout + millis();
  while (millis() < timeout) {
    checkSerialIO();
    checkTCPIO();
    delay(2);
  }
  Serial.println("delayAndHandleTask done");
  return false;
}

//检测串口消息
void checkSerialIO() {
  if (Serial.available()) //if number of bytes (characters) available for reading from serial port
  {
    char c = Serial.read();
    Serial.print("I received:"); //print I received
    Serial.println(c); //send what you read
    if (c == 't')testDisplay();
    if (c == 's')smartConfig(30);
  }
}

//-------------------------TCP START------------------------------
WiFiClient client;
//设备唯一标识,后台提前入库
const char* DEVICE_ID = "S5FE62HHYDBI";
String host = "192.168.168.119";
const uint16_t port = 9000;
//检测TCP连接消息
bool checkTCPIO() {
  //检测建立连接
  if (!client.connected()) {
    if (!client.connect(host, port)) {//5s timeout
      Serial.println("TCP connection failed->" + host + ":" + port);
      String id = DEVICE_ID;
      sendTCP("{\"id\":\"" + id + "\"}");
      return false;
    } else {
      Serial.println("TCP connection OK->" + host + ":" + port);
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
    String line = client.readStringUntil('\n');
    Serial.println("TCP read->" + line);
    client.print(line);
  }
}
//发送TCP消息
bool sendTCP(String msg) {
  Serial.println("sending data to server->" + msg);
  if (client.connected()) {
    client.println(msg);
    return true;
  } else {
    return false;
  }
}
//-------------------------TCP END------------------------------

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
  sendTubeCommand(6, humidity / 10);
  sendTubeCommand(5, humidity % 10);

  sendTubeCommand(4, 0xf);
  sendTubeCommand(3, 0xf);
  sendTubeCommand(2, temperature / 10);
  sendTubeCommand(1, temperature % 10);
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

void testDisplay()
{
  for (int i = 0; i < 256; i++) {
    sendTubeCommand(8, i / 1000 % 10);
    sendTubeCommand(7, i / 100 % 10);
    sendTubeCommand(6, i / 10 % 10);
    sendTubeCommand(5, i % 10);
    sendTubeCommand(4, 0xf);
    sendTubeCommand(3, 0xf);
    sendTubeCommand(2, 0xf);
    sendTubeCommand(1, i);
    delay(500);
    if (Serial.available()) {
      if (Serial.read() == 't') return;
    }
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
