#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"          //disable brownout problems
#include "soc/rtc_cntl_reg.h" //disable brownout problems
#include "esp_http_server.h"

//Replace with your network credentials
const char *ssid = "the robot network";
const char *password = "isaacasimov";

#define PART_BOUNDARY "123456789000000000000987654321"

// This project was tested with the AI Thinker Model, M5STACK PSRAM Model and M5STACK WITHOUT PSRAM
#define CAMERA_MODEL_AI_THINKER
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WITHOUT_PSRAM

// Not tested with this model
//#define CAMERA_MODEL_WROVER_KIT

#if defined(CAMERA_MODEL_WROVER_KIT)
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 21
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 19
#define Y4_GPIO_NUM 18
#define Y3_GPIO_NUM 5
#define Y2_GPIO_NUM 4
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#elif defined(CAMERA_MODEL_M5STACK_PSRAM)
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM 15
#define XCLK_GPIO_NUM 27
#define SIOD_GPIO_NUM 25
#define SIOC_GPIO_NUM 23

#define Y9_GPIO_NUM 19
#define Y8_GPIO_NUM 36
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 39
#define Y5_GPIO_NUM 5
#define Y4_GPIO_NUM 34
#define Y3_GPIO_NUM 35
#define Y2_GPIO_NUM 32
#define VSYNC_GPIO_NUM 22
#define HREF_GPIO_NUM 26
#define PCLK_GPIO_NUM 21

#elif defined(CAMERA_MODEL_M5STACK_WITHOUT_PSRAM)
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM 15
#define XCLK_GPIO_NUM 27
#define SIOD_GPIO_NUM 25
#define SIOC_GPIO_NUM 23

#define Y9_GPIO_NUM 19
#define Y8_GPIO_NUM 36
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 39
#define Y5_GPIO_NUM 5
#define Y4_GPIO_NUM 34
#define Y3_GPIO_NUM 35
#define Y2_GPIO_NUM 17
#define VSYNC_GPIO_NUM 22
#define HREF_GPIO_NUM 26
#define PCLK_GPIO_NUM 21

#elif defined(CAMERA_MODEL_AI_THINKER)
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
#else
#error "Camera model not selected"
#endif

static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;

camera_config_t config;

String pixel_format = "JPEG";
String frame_size = "UXGA";
int jpeg_quality = 10;
int fb_count = 2;
bool runSetConfig = false;

static void setConfig()
{
runSetConfig=true;

    Serial.printf("setConfig \n");

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
    config.xclk_freq_hz = 20000000;

    /*!< Format of the pixel data: PIXFORMAT_ + YUV422|GRAYSCALE|RGB565|JPEG  */
    if (pixel_format == "JPEG")
    {
        config.pixel_format = PIXFORMAT_JPEG;
    }
    else if (pixel_format == "RGB565")
    {
        config.pixel_format = PIXFORMAT_RGB565;
    }
    else if (pixel_format == "GRAYSCALE")
    {
        config.pixel_format = PIXFORMAT_GRAYSCALE;
    }
    else if (pixel_format == "YUV422")
    {
        config.pixel_format = PIXFORMAT_YUV422;
    }
    else
    {
        config.pixel_format = PIXFORMAT_JPEG;
    }

    /*!< Size of the output image: FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA  */
    if (frame_size == "UXGA")
    {
        config.frame_size = FRAMESIZE_UXGA;
    }
    else if (frame_size == "SXGA")
    {
        config.frame_size = FRAMESIZE_SXGA;
    }
    else if (frame_size == "XGA")
    {
        config.frame_size = FRAMESIZE_XGA;
    }
    else if (frame_size == "SVGA")
    {
        config.frame_size = FRAMESIZE_SVGA;
    }
    else if (frame_size == "VGA")
    {
        config.frame_size = FRAMESIZE_VGA;
    }
    else if (frame_size == "CIF")
    {
        config.frame_size = FRAMESIZE_CIF;
    }
    else if (frame_size == "QVGA")
    {
        config.frame_size = FRAMESIZE_QVGA;
    }
    else
    {
        config.frame_size = FRAMESIZE_UXGA;
    }

    config.jpeg_quality = jpeg_quality;
    config.fb_count = fb_count;

    // if (psramFound())
    // {
    //     Serial.println("psram Found");
    // config.frame_size = FRAMESIZE_UXGA;
    // config.jpeg_quality = 10;
    //     config.fb_count = 2;
    // }
    // else
    // {
    //     Serial.println("psram Not Found");
    //     config.frame_size = FRAMESIZE_SVGA;
    //     config.jpeg_quality = 12;
    //     config.fb_count = 1;
    // }
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[64];

    //see if there are any quersyting parameters
    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    char *buf;
    size_t buf_len;
    buf_len = httpd_req_get_url_query_len(req) + 1;

    if (buf_len > 1)
    {
        buf = (char *)malloc(buf_len);

        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            /*!< Format of the pixel data: PIXFORMAT_ + YUV422|GRAYSCALE|RGB565|JPEG  */
            /*!< Size of the output image: FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA  */

            Serial.printf("Found URL query => %s \n", buf);
            char param[32];
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "pixel_format", param, sizeof(param)) == ESP_OK)
            {
                pixel_format = param;

                Serial.printf("Found URL query parameter => pixel_format=%s \n", param);
            }
            if (httpd_query_key_value(buf, "frame_size", param, sizeof(param)) == ESP_OK)
            {
                frame_size = param;

                Serial.printf("Found URL query parameter => frame_size=%s \n", param);
            }
            if (httpd_query_key_value(buf, "jpeg_quality", param, sizeof(param)) == ESP_OK)
            {
                jpeg_quality = (int)param;

                Serial.printf("Found URL query parameter => jpeg_quality=%s \n", param);
            }
            if (httpd_query_key_value(buf, "fb_count", param, sizeof(param)) == ESP_OK)
            {
                fb_count = (int)param;

                Serial.printf("Found URL query parameter => fb_count=%s \n", param);
            }
        }
        // free(buf);
    }

    // Camera init
    if (runSetConfig== false)
    {
        setConfig();

        Serial.printf("esp_camera_init \n");

        esp_err_t err = esp_camera_init(&config);
        if (err != ESP_OK)
        {
            Serial.printf("Camera init failed with error 0x%x \n", err);
            return err;
        }
    }else {
       Serial.printf("already called esp_camera_init \n");  
    }

    Serial.printf("esp_camera_fb_get \n");

    /* Set some custom headers with a single frame */
    fb = esp_camera_fb_get();
    if (fb)
    {
        // char *buffer;

        // sprintf(buffer, "%zu", fb->width);

        // httpd_resp_set_hdr(req, "Image-Width", buffer);

        // sprintf(buffer, "%zu", fb->height);

        // httpd_resp_set_hdr(req, "Image-Height", buffer);

        // sprintf(buffer, "%x", fb->format);

        // httpd_resp_set_hdr(req, "Image-Format", buffer);

        //        Serial.printf("Width: %x \n", fb->width);
        // Serial.printf("format: %x \n", fb->format);
        //Serial.printf("format: %x \n", fb->format);
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);

    if (res != ESP_OK)
    {
        return res;
    }

    while (true)
    {
        fb = esp_camera_fb_get();
        if (!fb)
        {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
        }
        else
        {
            if (fb->width > 400)
            {
                if (fb->format != PIXFORMAT_JPEG)
                {
                    bool jpeg_converted = frame2jpg(fb, 95, &_jpg_buf, &_jpg_buf_len); //80
                    esp_camera_fb_return(fb);
                    fb = NULL;
                    if (!jpeg_converted)
                    {
                        Serial.println("JPEG compression failed");
                        res = ESP_FAIL;
                    }
                }
                else
                {
                    _jpg_buf_len = fb->len;
                    _jpg_buf = fb->buf;
                }
            }
        }

        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if (fb)
        {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        }
        else if (_jpg_buf)
        {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if (res != ESP_OK)
        {
            break;
        }
        //Serial.printf("MJPG: %uB\n",(uint32_t)(_jpg_buf_len));
    }
    return res;
}

void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};

    //Serial.printf("Starting web server on port: '%d'\n", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &index_uri);
    }
}

void setup()
{
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector

    Serial.begin(115200);
    Serial.setDebugOutput(false);

    // Wi-Fi connection
    WiFi.begin(ssid, password);
    WiFi.setHostname("ESP32-CAM-1");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");

    Serial.print("Camera Stream Ready! Go to: http://");
    Serial.println(WiFi.localIP());

    if (!MDNS.begin("ESP32-CAM-1"))
    {
        Serial.println("Error setting up MDNS responder!");
        while (1)
        {
            delay(1000);
        }
    }
    Serial.println("mDNS responder started");

    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);

    // Start streaming web server
    startCameraServer();
}

void loop()
{
    delay(1);
}