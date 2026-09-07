#include <Arduino.h>
#include <FastLED.h>
#include "Config.h"

// 1. Mảng chứa dữ liệu bóng LED (Tạo ra ở đây)
CRGB leds[NUM_LEDS];

// 2. BIẾN CẦU NỐI (Mượn từ main.cpp)
extern String cmdAction;
extern String cmdColor;
extern int cmdBrightness;

// Biến chạy màu cho hiệu ứng Nháy nhạc
uint8_t gHue = 0; 

void TaskLED(void *pvParameters) {
  Serial.println("💡 Đang khởi tạo bảng LED...");
  
  // Khởi tạo FastLED
  FastLED.addLeds<WS2812B, PIN_LED, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50); // Giới hạn độ sáng an toàn lúc khởi động
  FastLED.clear();
  FastLED.show();

  for (;;) {
    // ----------------------------------------------------
    // CHẾ ĐỘ 1: BẬT ĐÈN (Màu tĩnh)
    // ----------------------------------------------------
    if (cmdAction == "turn_on") {
      if (cmdColor == "red")       fill_solid(leds, NUM_LEDS, CRGB::Red);
      else if (cmdColor == "green")  fill_solid(leds, NUM_LEDS, CRGB::Green);
      else if (cmdColor == "blue")   fill_solid(leds, NUM_LEDS, CRGB::Blue);
      else if (cmdColor == "yellow") fill_solid(leds, NUM_LEDS, CRGB::Yellow);
      else if (cmdColor == "white")  fill_solid(leds, NUM_LEDS, CRGB::White);
      else                           fill_solid(leds, NUM_LEDS, CRGB::Purple); // Màu mặc định
      
      FastLED.setBrightness(cmdBrightness);
    } 
    
    // ----------------------------------------------------
    // CHẾ ĐỘ 2: TẮT ĐÈN
    // ----------------------------------------------------
    else if (cmdAction == "turn_off") {
      FastLED.clear(); 
    }

    
    // ----------------------------------------------------
    // CHẾ ĐỘ 3: NHÁY NHẠC (Party Mode / Visualizer)
    // ----------------------------------------------------
    else if (cmdAction == "music") {
      FastLED.setBrightness(cmdBrightness); // Giữ độ sáng theo giọng nói/web
      
      // Dùng biến cmdColor từ Node-RED để phân biệt dòng nhạc
      if (cmdColor == "lofi") {
        // 1. NHẠC LOFI / CHILL (Hiệu ứng nhịp thở - Breathe)
        // Ánh sáng phập phồng chậm rãi (15 nhịp/phút), màu xanh tím dịu mắt
        uint8_t pulse = beatsin8(15, 50, 255); 
        fill_solid(leds, NUM_LEDS, CHSV(160, 255, pulse)); 
      } 
      else if (cmdColor == "rock") {
        // 2. NHẠC ROCK / BASS MẠNH (Hiệu ứng chớp giật - Strobe)
        // Chớp tắt liên tục 120 nhịp/phút theo tone màu Đỏ - Đen
        uint8_t flash = beat8(120); 
        fill_solid(leds, NUM_LEDS, (flash > 128) ? CRGB::Red : CRGB::Black);
      }
      else {
        // 3. MẶC ĐỊNH LÀ EDM / REMIX (Cầu vồng cuộn + Flash chớp)
        fill_rainbow(leds, NUM_LEDS, gHue, 7);
        if(random8() < 120) { // Thỉnh thoảng có vài bóng chớp trắng lóa như sàn nhảy
            leds[random16(NUM_LEDS)] += CRGB::White;
        }
        gHue += 15; // Tốc độ cuộn màu nhanh
      }
    }

    // Xuất dữ liệu ra bảng LED
    FastLED.show();

    // 💡 LƯU Ý QUAN TRỌNG: 
    // Oi đã giảm delay từ 100ms xuống 30ms (Tương đương 33 FPS).
    // Việc này giúp hiệu ứng cầu vồng chạy mượt mà như video 60fps, 
    // nhưng vẫn đủ thời gian để nhường CPU cho kết nối mạng.
    vTaskDelay(30 / portTICK_PERIOD_MS); 
  }
}