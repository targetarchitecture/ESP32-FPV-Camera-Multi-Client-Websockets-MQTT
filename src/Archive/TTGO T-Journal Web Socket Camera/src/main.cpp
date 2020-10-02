#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "soc/soc.h"          // Disable brownout problems
#include "soc/rtc_cntl_reg.h" // Disable brownout  problems
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_I2CDevice.h>

const char *ssid = "the robot network";
const char *password = "isaacasimov";

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define I2C_SDA 14
#define I2C_SCL 13

// Select camera model
#define CAMERA_MODEL_TTGO_T_JOURNAL
#include "camera_pins.h"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

//holds the current upload
bool clientConnected[3] = {false, false, false};
uint8_t webSocketPort = 80;

WebSocketsServer webSocket = WebSocketsServer(webSocketPort);
WiFiClient client;

camera_fb_t *fb;
size_t fb_len;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
int initCamera();

void setup(void)
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector

  Serial.begin(115200);
  Serial.setDebugOutput(false);

  Serial.printf("Total heap: %d \n", ESP.getHeapSize());
  Serial.printf("Free heap: %d \n", ESP.getFreeHeap());

  // Init OLED
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false))
  {
    Serial.println(F("SSD1306 allocation failed"));
    delay(10000);
    ESP.restart();
  }

  //WIFI INIT
  Serial.printf("Connecting to %s\n", ssid);
  if (String(WiFi.SSID()) != String(ssid))
  {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
  }

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected! IP address: ");
  String ipAddress = WiFi.localIP().toString();
  Serial.println(ipAddress);

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
  case WStype_TEXT:
  case WStype_BIN:
  case WStype_ERROR:
  case WStype_FRAGMENT_TEXT_START:
  case WStype_FRAGMENT_BIN_START:
  case WStype_FRAGMENT:
  case WStype_FRAGMENT_FIN:
    break;
  }
}

void loop(void)
{
  webSocket.loop();

  //get the camera feed
  int64_t fr_start = esp_timer_get_time();

  fb = esp_camera_fb_get();

  if (!fb)
  {
    Serial.println("Frame buffer could not be acquired");
    delay(10000);
    ESP.restart();
  }

  fb_len = fb->len;

  for (int camNo = 0; camNo < 3; camNo++)
  {
    if (clientConnected[camNo] == true)
    {
      //replace this with your own function
      webSocket.sendBIN(camNo, fb->buf, fb_len);
    }
  }

  //return the frame buffer back to be reused
  esp_camera_fb_return(fb);

  int64_t fr_end = esp_timer_get_time();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 5);
  display.printf("JPG: %uB \n", (uint32_t)(fb_len));
  display.printf("%ums\n", (uint32_t)((fr_end - fr_start) / 1000));
  display.display();

  //Serial.printf("JPG: %uB %ums\n", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) / 1000));
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