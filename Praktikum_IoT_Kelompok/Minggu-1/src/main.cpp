#include <Arduino.h>
#include <DHT.h>
#include <ArduinoJson.h>

// Pin sesuai Lab-0
#define PIN_DHT 4
#define TIPE_DHT DHT22
#define PIN_LED_ALERT 2
#define PIN_TOMBOL_DARURAT 15

// 0 = blocking (delay), 1 = non-blocking (millis)
#define MODE_NON_BLOCKING 1

DHT dht(PIN_DHT, TIPE_DHT);

const unsigned long INTERVAL_NORMAL_MS = 2000;
const unsigned long INTERVAL_SIAGA_MS = 500;
const float AMBANG_SUHU_C = 32.0f;

unsigned long waktuBacaTerakhir = 0;
unsigned long nomorPaket = 0;
bool statusTombolSebelumnya = HIGH;
bool modeSiagaCepat = false;
bool tombolDaruratAktif = false;

void kirimTelemetriJson(float suhu, float kelembapan, bool alertSuhu) {
  JsonDocument doc;
  doc["device_id"] = "esp32-unib-node01";
  doc["seq"] = ++nomorPaket;
  doc["uptime_ms"] = millis();
  doc["mode"] = MODE_NON_BLOCKING ? "non-blocking" : "blocking";
  doc["telemetry_interval_ms"] = modeSiagaCepat ? INTERVAL_SIAGA_MS : INTERVAL_NORMAL_MS;
  doc["temp_c"] = suhu;
  doc["hum_pct"] = kelembapan;
  doc["alert_temp"] = alertSuhu;
  doc["emergency_button"] = tombolDaruratAktif;
  doc["rapid_mode"] = modeSiagaCepat;
  serializeJson(doc, Serial);
  Serial.println();
}

void prosesTombol() {
  bool statusTombol = digitalRead(PIN_TOMBOL_DARURAT);

  // Deteksi tepi tekan dengan debounce singkat
  if (statusTombol == LOW && statusTombolSebelumnya == HIGH) {
    delay(50);
    if (digitalRead(PIN_TOMBOL_DARURAT) == LOW) {
      tombolDaruratAktif = !tombolDaruratAktif;
      modeSiagaCepat = tombolDaruratAktif;
      Serial.printf(
        "{\"event\":\"emergency_button\",\"pressed_at_ms\":%lu,\"rapid_mode\":%s}\n",
        millis(), modeSiagaCepat ? "true" : "false"
      );
    }
  }
  statusTombolSebelumnya = statusTombol;
}

void bacaDanKirim() {
  float kelembapan = dht.readHumidity();
  float suhu = dht.readTemperature();

  if (isnan(suhu) || isnan(kelembapan)) {
    Serial.println("{\"error\":\"Pembacaan DHT22 gagal\"}");
    return;
  }

  bool alertSuhu = suhu >= AMBANG_SUHU_C;
  digitalWrite(PIN_LED_ALERT, (alertSuhu || tombolDaruratAktif) ? HIGH : LOW);
  kirimTelemetriJson(suhu, kelembapan, alertSuhu);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_ALERT, OUTPUT);
  pinMode(PIN_TOMBOL_DARURAT, INPUT_PULLUP);

  digitalWrite(PIN_LED_ALERT, LOW);
  pinMode(PIN_DHT, INPUT_PULLUP); // Pull-up internal pada jalur data DHT22
  dht.begin();

  Serial.println("{\"event\":\"boot\",\"msg\":\"Simpul IoT UNIB Aktif\"}");
  delay(2000);
  waktuBacaTerakhir = millis();
}

void loop() {
#if MODE_NON_BLOCKING == 0
  // Mode blocking untuk pembanding eksperimen.
  prosesTombol();
  delay(modeSiagaCepat ? INTERVAL_SIAGA_MS : INTERVAL_NORMAL_MS);
  bacaDanKirim();
#else
  // Mode non-blocking: tombol tetap diperiksa di setiap putaran loop.
  prosesTombol();
  unsigned long sekarang = millis();
  unsigned long intervalAktif = modeSiagaCepat ? INTERVAL_SIAGA_MS : INTERVAL_NORMAL_MS;

  if (sekarang - waktuBacaTerakhir >= intervalAktif) {
    waktuBacaTerakhir = sekarang;
    bacaDanKirim();
  }
#endif
}
