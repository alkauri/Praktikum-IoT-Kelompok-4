#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include "esp_sleep.h"

// Konfigurasi Lab-2
#define LEBAR_LAYAR 128
#define TINGGI_LAYAR 64
#define ALAMAT_OLED 0x3C
#define ALAMAT_BMP 0x76
#define PIN_LED_STATUS 2
#define PIN_WAKEUP_TOMBOL GPIO_NUM_15
#define DURASI_TIDUR_DETIK 10

Adafruit_SSD1306 display(LEBAR_LAYAR, TINGGI_LAYAR, &Wire, -1);
Adafruit_BMP280 bmp;

// Data persisten selama deep sleep melalui RTC slow memory.
RTC_DATA_ATTR int nomorSiklusBoot = 0;
RTC_DATA_ATTR float riwayatSuhuTerakhir = 0.0f;
RTC_DATA_ATTR float riwayatTekananTerakhir = 0.0f;

// C6: rekor suhu sepanjang siklus deep sleep (reset hanya saat power cycle).
RTC_DATA_ATTR float suhuMaks = -1000.0f;
RTC_DATA_ATTR float suhuMin = 1000.0f;
RTC_DATA_ATTR bool adaRekorSuhu = false;

struct DataSensor {
  float suhu;
  float tekanan;
  float elevasi;
  bool valid;
};
DataSensor dataCuaca = {0, 0, 0, false};

SemaphoreHandle_t mutexData;

void pindaiBusI2C() {
  Serial.println("[I2C] Memulai pemindaian alamat...");
  for (uint8_t alamat = 1; alamat < 127; alamat++) {
    Wire.beginTransmission(alamat);
    uint8_t hasil = Wire.endTransmission();
    if (hasil == 0) {
      Serial.printf("[I2C] Peranti ditemukan pada alamat 0x%02X\n", alamat);
    }
  }
  Serial.println("[I2C] Pemindaian selesai.");
}

const char* labelWakeup() {
  esp_sleep_wakeup_cause_t alasan = esp_sleep_get_wakeup_cause();
  if (alasan == ESP_SLEEP_WAKEUP_TIMER) return "TIMER";
  if (alasan == ESP_SLEEP_WAKEUP_EXT0) return "EXT0 BTN";
  return "POWER-ON";
}

void cetakDiagnostikWakeup() {
  Serial.println("\n========================================");
  Serial.printf("[RTC] Siklus Bangun Ke-%d\n", nomorSiklusBoot);
  Serial.printf("[RTC WAKEUP] Pemicu: %s\n", labelWakeup());
  Serial.printf("[RTC MEM] Suhu lalu: %.2f C | Tekanan lalu: %.2f hPa\n",
                riwayatSuhuTerakhir, riwayatTekananTerakhir);
  if (adaRekorSuhu) {
    Serial.printf("[REKOR] Suhu maksimum: %.2f C | minimum: %.2f C\n", suhuMaks, suhuMin);
  } else {
    Serial.println("[REKOR] Belum ada data suhu tersimpan.");
  }
}

void perbaruiTampilanOled(const DataSensor &data, int siklus, const char* pemicu) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("UNIB IoT - STASIUN CUACA");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(0, 13);
  display.printf("Suhu     : %.1f C", data.suhu);
  display.setCursor(0, 24);
  display.printf("Tekanan  : %.1f hPa", data.tekanan);
  display.setCursor(0, 35);
  display.printf("Elevasi  : %.1f m", data.elevasi);

  display.drawLine(0, 46, 127, 46, SSD1306_WHITE);
  display.setCursor(0, 48);
  display.printf("Max:%.1fC Min:%.1fC", suhuMaks, suhuMin);
  display.setCursor(0, 57);
  display.printf("#%d | %s", siklus, pemicu);
  display.display();
}

// Task UI/JSON di Core 0; akuisisi sensor dilakukan setup() pada Core 1.
void TaskTampilanDanSerial(void *parameter) {
  DataSensor lokal;
  xSemaphoreTake(mutexData, portMAX_DELAY);
  lokal = dataCuaca;
  xSemaphoreGive(mutexData);

  if (lokal.valid) {
    perbaruiTampilanOled(lokal, nomorSiklusBoot, labelWakeup());

    JsonDocument doc;
    doc["device_id"] = "esp32-weather-st01";
    doc["boot_count"] = nomorSiklusBoot;
    doc["wakeup_cause"] = labelWakeup();
    doc["temp_c"] = lokal.suhu;
    doc["pressure_hpa"] = lokal.tekanan;
    doc["altitude_m"] = lokal.elevasi;
    doc["prev_temp_c"] = riwayatSuhuTerakhir;
    doc["temp_max_c"] = suhuMaks;
    doc["temp_min_c"] = suhuMin;
    serializeJson(doc, Serial);
    Serial.println();

    riwayatSuhuTerakhir = lokal.suhu;
    riwayatTekananTerakhir = lokal.tekanan;
  }
  vTaskDelete(NULL);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_WAKEUP_TOMBOL, INPUT_PULLUP);
  digitalWrite(PIN_LED_STATUS, HIGH);

  nomorSiklusBoot++;
  cetakDiagnostikWakeup();

  mutexData = xSemaphoreCreateMutex();
  Wire.begin(21, 22); // SDA GPIO 21, SCL GPIO 22
  pindaiBusI2C();

  if (!display.begin(SSD1306_SWITCHCAPVCC, ALAMAT_OLED)) {
    Serial.println("[ERROR] Gagal menemukan OLED SSD1306 pada 0x3C.");
  }

  if (!bmp.begin(ALAMAT_BMP)) {
    Serial.println("[ERROR] BMP280 tidak ditemukan pada 0x76.");
    dataCuaca.valid = false;
  } else {
    bmp.setSampling(Adafruit_BMP280::MODE_FORCED,
                    Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);
    bmp.takeForcedMeasurement();

    xSemaphoreTake(mutexData, portMAX_DELAY);
    dataCuaca.suhu = bmp.readTemperature();
    dataCuaca.tekanan = bmp.readPressure() / 100.0F;
    dataCuaca.elevasi = bmp.readAltitude(1013.25F);
    dataCuaca.valid = true;

    // Perbarui rekor ekstrem suhu yang persisten di RTC.
    if (!adaRekorSuhu) {
      suhuMaks = dataCuaca.suhu;
      suhuMin = dataCuaca.suhu;
      adaRekorSuhu = true;
    } else {
      if (dataCuaca.suhu > suhuMaks) suhuMaks = dataCuaca.suhu;
      if (dataCuaca.suhu < suhuMin) suhuMin = dataCuaca.suhu;
    }
    xSemaphoreGive(mutexData);
  }

  // Core 0 menangani OLED dan Serial; sensor dibaca pada setup() Arduino Core 1.
  xTaskCreatePinnedToCore(TaskTampilanDanSerial, "TaskUI", 4096, NULL, 1, NULL, 0);
  delay(700); // Beri waktu task OLED/JSON menyelesaikan tugasnya.

  Serial.printf("[POWER] Menyiapkan Deep Sleep %d detik...\n", DURASI_TIDUR_DETIK);
  esp_sleep_enable_timer_wakeup((uint64_t)DURASI_TIDUR_DETIK * 1000000ULL);
  esp_sleep_enable_ext0_wakeup(PIN_WAKEUP_TOMBOL, 0);

  digitalWrite(PIN_LED_STATUS, LOW);
  Serial.println("[POWER] Memasuki Deep Sleep. LED status mati.");
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {
  // Sistem bekerja dengan pola bangun-baca-tampilkan-tidur.
}
