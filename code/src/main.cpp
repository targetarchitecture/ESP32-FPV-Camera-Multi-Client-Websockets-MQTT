#include <Arduino.h>
#include "esp_camera.h"
#include "credentials.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_I2CDevice.h>
#include <PubSubClient.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <string>
#include <regex>
#include "MovingAverage.h"

// Select camera model - LILYGO® TTGO T-Journal
#define CAMERA_MODEL_TTGO_T_JOURNAL
#include "camera_pins.h"

// Set the OLED parameters
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define I2C_SDA 14
#define I2C_SCL 13

//averaging calculations
#define framesToAvg 32
MovingAverage<uint8_t, framesToAvg> loopAvg;
MovingAverage<uint8_t, framesToAvg> mqttAvg;
MovingAverage<uint8_t, framesToAvg> wsAvg;
uint32_t timeToCompleteMQTTMs = 0;
int8_t previousWebSocketClients = -1;
ulong previousMQTTmillis = 0;
uint8_t MQTTFPS = 0;
uint8_t FPS = 0;
ulong MQTTFPSmillis = millis();

//define port numbers
#define webSocketPort 81
#define webServerPort 80

//object declaration
WebSocketsServer webSocket = WebSocketsServer(webSocketPort);
WebServer server(webServerPort);
WiFiClient client;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

//MQTT definitions
PubSubClient MQTTClient;

//embedded files
extern const uint8_t vrHTML_start[] asm("_binary_www_vr_html_start");
extern const uint8_t vrHTML_end[] asm("_binary_www_vr_html_end");
extern const uint8_t cocossdHTML_start[] asm("_binary_www_cocossd_html_start");
extern const uint8_t cocossdHTML_end[] asm("_binary_www_cocossd_html_end");
extern const uint8_t fullscreenHTML_start[] asm("_binary_www_fullscreen_html_start");
extern const uint8_t fullscreenHTML_end[] asm("_binary_www_fullscreen_html_end");

//function declaration
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
int initCamera();
void handleRoot();
void handleCocossd();
void handleFullScreen();
void handle404();
int calculateAVGFPS(int frameTime);

void setup(void)
{
  //disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  //turn off bluetooth
  btStop();

  //start your serial port
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  // Init I2C bus & OLED
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false))
  {
    Serial.println(F("SSD1306 allocation failed"));
    delay(10000);
    ESP.restart();
  }

  //WIFI start up
  Serial.printf("Connecting to %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  //connect
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.println(".");
  }

  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(5, 5);
  display.print(WiFi.localIP());
  display.display();

  int cameraInitState = initCamera();

  Serial.printf("camera init state %d\n", cameraInitState);

  if (cameraInitState != 0)
  {
    delay(10000);
    ESP.restart();
  }

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  //set this to be a large enough value to allow an MQTT message containing a 22Kb JPEG to be sent
  MQTTClient.setBufferSize(30000);

  Serial.println("Connecting to MQTT server");
  MQTTClient.setClient(client);
  MQTTClient.setServer(MQTT_SERVER, 1883);

  Serial.println("connect mqtt...");
  if (MQTTClient.connect(MQTT_CLIENTID, MQTT_USERNAME, MQTT_KEY))
  {
    Serial.println("Connected to MQTT server");
  }

  MQTTClient.publish(MQTT_IP_TOPIC, WiFi.localIP().toString().c_str(),true);

  //send urls to MQTT
  String url = WiFi.localIP().toString();
  url.concat("/");

  MQTTClient.publish(MQTT_URL_TOPIC, url.c_str(),true);

  url = WiFi.localIP().toString();
  url.concat("/cocossd");

  MQTTClient.publish(MQTT_COCOSSD_TOPIC, url.c_str(),true);

  url = WiFi.localIP().toString();
  url.concat("/fullscreen");

  MQTTClient.publish(MQTT_FULLSCREEN_TOPIC, url.c_str());

  //define web server endpoints and 404
  server.on("/", handleRoot);
  server.on("/cocossd", handleCocossd);
  server.on("/fullscreen", handleFullScreen);
  server.onNotFound(handle404);

  //start the web server
  server.begin();
  Serial.println("HTTP server started");

  MQTTClient.publish(MQTT_INFO_TOPIC, "HTTP server started");

  //some info
  Serial.printf("Total heap: %d \n", ESP.getHeapSize());
  Serial.printf("Free heap: %d \n", ESP.getFreeHeap());
}

void loop(void)
{
  //get the camera feed
  int64_t loop_start = esp_timer_get_time();

  webSocket.loop();
  server.handleClient();

  camera_fb_t *fb = NULL;
  fb = esp_camera_fb_get();

  if (!fb)
  {
    MQTTClient.publish(MQTT_ERROR_TOPIC, "Frame buffer could not be acquired",true);

    Serial.println("Frame buffer could not be acquired");
    delay(10000);
    ESP.restart();
  }

  uint32_t fb_len = fb->len;

  unsigned long currentMillis = millis();

  //200 = 5 FPS
  if (currentMillis - previousMQTTmillis >= 200)
  {
    // save the last time an MQTT message was sent
    previousMQTTmillis = currentMillis;

    int64_t MQTT_start = esp_timer_get_time();

    if (MQTTClient.connected())
    {
      MQTTClient.publish(MQTT_FRAMES_TOPIC, fb->buf, fb_len);
    }
    else
    {
      if (MQTTClient.connect(MQTT_CLIENTID, MQTT_USERNAME, MQTT_KEY))
      {
        Serial.println("Reconnected");
      }
    }

    int64_t MQTT_end = esp_timer_get_time();

    timeToCompleteMQTTMs = mqttAvg.add((MQTT_end - MQTT_start) / 1000);
  }

  currentMillis = millis();

  if (currentMillis - MQTTFPSmillis >= 1000)
  {
    String sFPS = "";
    sFPS.concat(MQTTFPS);
    FPS = MQTTFPS;

    MQTTClient.publish(MQTT_FPS_TOPIC, sFPS.c_str());

    MQTTFPSmillis = currentMillis;
    MQTTFPS = 0;
  }
  else
  {
    MQTTFPS = MQTTFPS + 1;
  }

  int64_t ws_start = esp_timer_get_time();

  //broadcast to all connected clients
  webSocket.broadcastBIN(fb->buf, fb_len);

  int64_t ws_end = esp_timer_get_time();
  uint8_t timeToCompleteWSMs = wsAvg.add((ws_end - ws_start) / 1000);

  //return the frame buffer back to be reused
  esp_camera_fb_return(fb);

  //put some delay in here, give the poor ESP a rest (not simple as the loop takes differant times to complete based on the image size being sent to the MQTT server and WS clients)
  if (webSocket.connectedClients() == 0)
  {
    delay(100);
  }
  else if (webSocket.connectedClients() == 1)
  {
    delay(20);
  }
  else if (webSocket.connectedClients() == 2)
  {
    delay(10);
  }
  else
  {
    delay(5);
  }

  //do loop calculations and display
  int64_t loop_end = esp_timer_get_time();
  uint8_t timeToCompleteLoopMs = loopAvg.add((loop_end - loop_start) / 1000);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.printf("JPEG: %u Kb\n", (uint32_t)(fb_len) / 1024);
  display.printf("MQTT: %u ms WS: %u ms\n", timeToCompleteMQTTMs, timeToCompleteWSMs);
  display.printf("LOOP TIME: %u ms\n", timeToCompleteLoopMs);
  display.printf("CLIENTS: %u  FPS: %u\n", webSocket.connectedClients(), FPS);
  display.display();

  if (previousWebSocketClients != webSocket.connectedClients())
  {
    String msg = "";
    msg.concat(webSocket.connectedClients());

    MQTTClient.publish(MQTT_WS_TOPIC, msg.c_str());

    previousWebSocketClients = webSocket.connectedClients();
  }
}

int initCamera()
{
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 15;
  config.fb_count = 2;

  // camera init
  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(10000);
    ESP.restart();
  }

  sensor_t *s = esp_camera_sensor_get();
  s->set_framesize(s, config.frame_size);

  return 0;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_DISCONNECTED:
    Serial.printf("[%u] Disconnected!\n", num);
    break;
  case WStype_CONNECTED:
    Serial.printf("[%u] Connected Client Id!\n", num);
    break;
  }
}

void handleRoot()
{
  auto html = (const char *)vrHTML_start;

  auto resolved = std::regex_replace(html, std::regex("\\{\\{IP\\}\\}"), WiFi.localIP().toString().c_str());

  auto port = (String)webSocketPort;

  resolved = std::regex_replace(resolved, std::regex("\\{\\{PORT\\}\\}"), port.c_str());

  server.send(200, "text/html", resolved.c_str());
}

void handleCocossd()
{
  auto html = (const char *)cocossdHTML_start;

  auto resolved = std::regex_replace(html, std::regex("\\{\\{IP\\}\\}"), WiFi.localIP().toString().c_str());

  auto port = (String)webSocketPort;

  resolved = std::regex_replace(resolved, std::regex("\\{\\{PORT\\}\\}"), port.c_str());

  server.send(200, "text/html", resolved.c_str());
}

void handleFullScreen()
{
  auto html = (const char *)fullscreenHTML_start;

  auto resolved = std::regex_replace(html, std::regex("\\{\\{IP\\}\\}"), WiFi.localIP().toString().c_str());

  auto port = (String)webSocketPort;

  resolved = std::regex_replace(resolved, std::regex("\\{\\{PORT\\}\\}"), port.c_str());

  server.send(200, "text/html", resolved.c_str());
}

void handle404()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}