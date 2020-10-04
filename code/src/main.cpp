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

// Select camera model - LILYGO® TTGO T-Journal
#define CAMERA_MODEL_TTGO_T_JOURNAL
#include "camera_pins.h"

// comment out if MQTT not required
#define USE_MQTT

// Set the OLED parameters
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define I2C_SDA 14
#define I2C_SCL 13

//averaging calculation
#define framesToAvg 100
int frameTimings[framesToAvg];
int frameIndex = 0;

//holds the webSocket client tracker (TODO: improve design so more than 3 websockets can be used)
bool clientConnected[3] = {false, false, false};

//define port numbers
auto webSocketPort = 81;
auto webServerPort = 80;

//object declaration
WebSocketsServer webSocket = WebSocketsServer(webSocketPort);
WebServer server(webServerPort);
WiFiClient client;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

//MQTT definitions
#if defined(USE_MQTT)
PubSubClient MQTTClient;
#endif

//embedded files
extern const uint8_t vrHTML_start[] asm("_binary_www_vr_html_start");
extern const uint8_t vrHTML_end[] asm("_binary_www_vr_html_end");

//function declaration
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
int initCamera();
void handleRoot();
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
  Serial.printf("Connecting to %s\n", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

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
  display.print(":");
  display.println(webSocketPort);
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

//only include if using MQTT
#if defined(USE_MQTT)
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
#endif

  //define web server endpoints and 404
  server.on("/", handleRoot);
  server.onNotFound(handle404);

  server.begin();
  Serial.println("HTTP server started");

  //some info
  Serial.printf("Total heap: %d \n", ESP.getHeapSize());
  Serial.printf("Free heap: %d \n", ESP.getFreeHeap());
}

void loop(void)
{
  webSocket.loop();
  server.handleClient();

  //get the camera feed
  int64_t fr_start = esp_timer_get_time();

  camera_fb_t *fb = NULL;
  fb = esp_camera_fb_get();

  if (!fb)
  {
    Serial.println("Frame buffer could not be acquired");
    delay(10000);
    ESP.restart();
  }

  uint32_t fb_len = fb->len;

  // if (fb_len > max_fb_len)
  // {
  //   max_fb_len = fb_len;
  //   Serial.print("Framebuffer Length : ");
  //   Serial.println(max_fb_len);
  // }

  uint32_t webSockets = 0;

  for (int camNo = 0; camNo < 3; camNo++)
  {
    if (clientConnected[camNo] == true)
    {
      webSockets++;

      webSocket.sendBIN(camNo, fb->buf, fb_len);
    }
  }

//only include if using MQTT
#if defined(USE_MQTT)

  if (MQTTClient.connected())
  {
    MQTTClient.publish(MQTT_TOPIC, fb->buf, fb_len);
  }
  else
  {
    if (MQTTClient.connect(MQTT_CLIENTID, MQTT_USERNAME, MQTT_KEY))
    {
      Serial.println("Reconnected");
    }
  }
#endif

  delay(10);

  //return the frame buffer back to be reused
  esp_camera_fb_return(fb);

  int64_t fr_end = esp_timer_get_time();
  uint32_t timeToCompleteLoopMs = ((fr_end - fr_start) / 1000);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.printf("JPEG: %u Kb\n", (uint32_t)(fb_len) / 1024);
  display.printf("TIME: %u ms\n", timeToCompleteLoopMs);
  display.printf("CLIENTS: %u\n", webSockets);
  display.printf("FPS: %u\n", calculateAVGFPS(timeToCompleteLoopMs));
  display.display();
}

int calculateAVGFPS(int frameTime)
{
  if (frameIndex == framesToAvg)
  {
    frameIndex = 0;
  }

  frameTimings[frameIndex] = frameTime;

  frameIndex++;

  //loop through and get average
  int sum = 0;
  for (int i = 0; i < framesToAvg; i++)
  {
    sum += frameTimings[i];
  }

  return (int)sum / framesToAvg;
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
    if (num < 3)
    {
      clientConnected[num] = false;
    }
    break;
  case WStype_CONNECTED:
    Serial.printf("[%u] Connected Client Id!\n", num);
    if (num < 3)
    {
      clientConnected[num] = true;
    }
    break;
  }
}


void handleRoot()
{
  auto html = (const char *)vrHTML_start;

  auto resolved = std::regex_replace(html, std::regex("\\{\\{IP\\}\\}"), WiFi.localIP().toString().c_str());

  auto port = (String)webSocketPort;

  resolved = std::regex_replace(resolved, std::regex("\\{\\{PORT\\}\\}"), port.c_str());

  //Serial.println(resolved.c_str());

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