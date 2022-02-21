/*
    This sketch sends data via HTTP GET requests to data.sparkfun.com service.

    You need to get streamId and privateKey at data.sparkfun.com and paste them
    below. Or just customize this script to talk to other HTTP servers.

*/

#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_LEDBackpack.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>
#include "esp_sntp.h"

// Use dedicated hardware SPI pins
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

#define PPS_OFFSET_MS 14
#define US_IN_S (1000000)

#define NEOPIXEL_PIN   33
#define NEOPIXEL_POWER 34
// How many NeoPixels are attached to the Arduino?
#define NEOPIXEL_COUNT 1

// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

Adafruit_AlphaNum4 alpha4;
Adafruit_7segment seg1, seg2;

#include "wifi_config.h" // not committed to git, make your own
// defines character constants `ntpserver`, `ssid` and `password`.

bool ever_set;
struct timeval last_set;
static void time_sync_notification_cb(struct timeval *tv)
{
  ever_set = true;
  gettimeofday(&last_set, NULL);
  sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
}

// Dimensions the buffer that the task being created will use as its stack.
// NOTE:  This is the number of bytes the stack will hold, not the number of
// words as found in vanilla FreeRTOS.
#define STACK_SIZE 6000

// Task handles
TaskHandle_t wifi_task = NULL, tft_task = NULL;

// Task control blocks
StaticTask_t wifi_taskbuf, tft_taskbuf;

// Task stacks
StackType_t wifi_stack[ STACK_SIZE ], tft_stack[ STACK_SIZE ];

void wifi_func(void *unused) {
  // We start by connecting to a WiFi network

  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Initializing SNTP");
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
  sntp_setservername(0, ntpserver);
  sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  sntp_set_sync_interval(15 * 1000); // minimum
  sntp_init();

  vTaskDelete(NULL); // exit this task
}

void setup()
{
  Serial.begin(115200);
  delay(10);

  // Create the task without using any dynamic memory allocation.
  wifi_task = xTaskCreateStatic(
              wifi_func,       // Function that implements the task.
              "NAME",          // Text name for the task.
              sizeof wifi_stack, // Stack size in bytes, not words.
              ( void * ) 1,    // Parameter passed into the task.
              tskIDLE_PRIORITY,// Priority at which the task is created.
              wifi_stack,      // Array to use as the task's stack.
              &wifi_taskbuf ); // Variable to hold the task's data structure.

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, HIGH);

  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1);
  tzset();

  alpha4.begin(0x70);  // pass in the address
  seg1.begin(0x74);
  seg2.begin(0x75);
  seg1.drawColon(true);


  // turn on backlite
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);

  // turn on the TFT / I2C power supply
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);

  // Create the task without using any dynamic memory allocation.
  tft_task = xTaskCreateStatic(
              tft_func,        // Function that implements the task.
              "NAME",          // Text name for the task.
              sizeof tft_stack, // Stack size in bytes, not words.
              ( void * ) 1,    // Parameter passed into the task.
              tskIDLE_PRIORITY,// Priority at which the task is created.
              tft_stack,       // Array to use as the task's stack.
              &tft_taskbuf );  // Variable to hold the task's data structure.

}

void tft_func(void *) {
  // initialize TFT
  tft.init(135, 240); // Init ST7789 240x135
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.cp437(true);
  tft.setTextSize(2);

  while(true) {
    struct timeval tv;
    auto set_row = [](int row) { tft.setCursor(0, 16*row); };

    set_row(0);
    if (WiFi.status() == WL_CONNECTED) {
      tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      tft.printf("IP: %16.16s\n", WiFi.localIP().toString().c_str());
    } else {
      tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
      tft.printf("WIFI Not Connected\n");
    }
    
    if (ever_set) {
      tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      char buf[32];
      struct tm when;
      localtime_r(&last_set.tv_sec, &when);
      strftime(buf, sizeof(buf), "%T", &when);
      set_row(1);
      tft.printf("Set at %s\n", buf);

      set_row(2);
      tft.printf("Sync ");
      auto status = sntp_get_sync_status();
      if(status == SNTP_SYNC_STATUS_IN_PROGRESS) {
        adjtime(NULL, &tv);
        long long d = tv.tv_usec + (long long)tv.tv_sec * US_IN_S;
        Serial.printf("%lld - %ds+%dus\n", d, tv.tv_sec, tv.tv_usec);
        if(llabs(d) > 100*US_IN_S) {
          tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
          tft.printf("??????????? us\n");
        } else {
          tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
          tft.printf("%+11lld us\n", d);
        }
      } else {
        tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
        tft.printf("%-15s\n", "complete");
      }

      set_row(6);
      gettimeofday(&tv, NULL);
      gmtime_r(&tv.tv_sec, &when);
      strftime(buf, sizeof(buf), "%FT%TZ", &when);
      tft.printf("%s\n", buf);
    } else {
      tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
      set_row(6);
      tft.printf("Time Never Set\n");
    }
    yield();
  }
}

const char wdaynameshort[] = "Sun\0Mon\0Tue\0Wed\0Thu\0Fri\0Sat";

void loop()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  const unsigned duty = 100000;
  bool state = tv.tv_usec < duty;
  digitalWrite(LED_BUILTIN, state);

  if (!ever_set) {
    strip.setPixelColor(0, 0x010000);
  } else if (last_set.tv_sec < tv.tv_sec && last_set.tv_sec > tv.tv_sec - 10) {
    strip.setPixelColor(0, 0x000001);
  } else {
    strip.setPixelColor(0, 0x000100);
  }
  strip.show();

  if (ever_set) {
    struct tm when;
    localtime_r(&tv.tv_sec, &when);

    // apply the OFFSET
    tv.tv_usec += PPS_OFFSET_MS * 1000;
    if (tv.tv_usec < 0) {
      tv.tv_sec -= 1;
      tv.tv_usec += US_IN_S;
    } else if (tv.tv_usec > US_IN_S) {
      tv.tv_sec += 1;
      tv.tv_usec -= US_IN_S;
    }


    for (int i = 0; i < 3; i++) alpha4.writeDigitAscii(i + 1, wdaynameshort[4 * when.tm_wday + i]);
    alpha4.writeDigitAscii(0, ' ');

    char buf[5];
    int n = when.tm_hour * 100 + when.tm_min;
    seg1.println(n);

    if (when.tm_hour == 0) {
      seg1.writeDigitRaw(0, 0);
    } else {
      seg1.writeDigitNum(0, when.tm_hour / 10);
    }
    seg1.writeDigitNum(1, when.tm_hour % 10);
    seg1.writeDigitNum(3, when.tm_min / 10);
    seg1.writeDigitNum(4, when.tm_min % 10);
    seg1.drawColon(true);

    snprintf(buf, sizeof(buf), "%02d%02d", when.tm_sec, tv.tv_usec / 10 / 1000);
    seg2.writeDigitNum(1, when.tm_sec / 10);
    seg2.writeDigitNum(3, when.tm_sec % 10, true);
    int hundredths = tv.tv_usec / 10 / 1000;
    seg2.writeDigitNum(4, hundredths / 10);
    //seg2.writeDigitNum(4, hundredths % 10);

    seg2.writeDisplay();
    seg1.writeDisplay();
    alpha4.writeDisplay();

    if (when.tm_hour > 7 && when.tm_hour <= 21) {
      seg1.setBrightness(12);
      seg2.setBrightness(12);
      alpha4.setBrightness(12);
    } else {
      seg1.setBrightness(1);
      seg2.setBrightness(1);
      alpha4.setBrightness(1);
    }

  }

  gettimeofday(&tv, NULL);

  long sleep_us = 10000 - (tv.tv_usec + PPS_OFFSET_MS * 1000 + 10000) % 10000;
  usleep(sleep_us);
}
