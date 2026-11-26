#include <Arduino.h>
#include <DHT.h>
#include <ArduinoJson.h>

// Pinout Lab-1
#define PIN_DHT 4
#define TIPE_DHT DHT22
#define PIN_SOIL_ADC1 34
#define PIN_TOMBOL_MODE 15
#define PIN_RELAY_POMPA 26
#define PIN_PWM_KIPAS 18
#define PIN_LED_FAULT 19

// 0 = ambang tunggal (uji chattering), 1 = histeresis dua ambang
#define GUNAKAN_HISTERESIS 1

const int MV_TANAH_KERING = 2650;
const int MV_TANAH_BASAH = 1100;
const float AMBANG_POMPA_ON = 35.0f;
const float AMBANG_POMPA_OFF = 60.0f;
const float AMBANG_TUNGGAL = 45.0f;

const int PWM_FREQ_HZ = 5000;
const int PWM_RES_BIT = 8;
const int PWM_KANAL = 0;

const int UKURAN_FILTER = 5;
uint32_t bufferMv[UKURAN_FILTER] = {0};
int indeksFilter = 0;
bool filterPenuh = false;

DHT dht(PIN_DHT, TIPE_DHT);
const unsigned long INTERVAL_BACA_MS = 1000;
const unsigned long DEBOUNCE_MS = 60;
const unsigned long BATAS_POMPA_MS = 15000;
const float KENAIKAN_MIN_SOIL_PCT = 1.0f;

unsigned long waktuBacaTerakhir = 0;
unsigned long waktuDebounceTombol = 0;
unsigned long seqPaket = 0;
unsigned long hitungTransisiRelai = 0;
unsigned long waktuMulaiPompa = 0;
unsigned long waktuBlinkTerakhir = 0;

bool statusPompa = false;
bool modeOverridePaksa = false;
bool tombolSebelumnya = HIGH;
bool faultTerkunci = false;
bool ledFaultAktif = false;
float soilPctSaatPompaMulai = 0.0f;
float soilPctTertinggiSaatPompa = 0.0f;
const char* kodeFault = nullptr;

void setupPwmKipas() {
#if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0))
  ledcAttach(PIN_PWM_KIPAS, PWM_FREQ_HZ, PWM_RES_BIT);
#else
  ledcSetup(PWM_KANAL, PWM_FREQ_HZ, PWM_RES_BIT);
  ledcAttachPin(PIN_PWM_KIPAS, PWM_KANAL);
#endif
}

void tulisPwmKipas(uint8_t duty) {
#if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0))
  ledcWrite(PIN_PWM_KIPAS, duty);
#else
  ledcWrite(PWM_KANAL, duty);
#endif
}

uint32_t bacaSoilMilliVoltsTerfilter() {
  uint32_t mvMentah = analogReadMilliVolts(PIN_SOIL_ADC1);
  bufferMv[indeksFilter] = mvMentah;
  indeksFilter = (indeksFilter + 1) % UKURAN_FILTER;
  if (indeksFilter == 0) filterPenuh = true;

  int jumlahSampel = filterPenuh ? UKURAN_FILTER : indeksFilter;
  uint64_t total = 0;
  for (int i = 0; i < jumlahSampel; i++) total += bufferMv[i];
  return (uint32_t)(total / jumlahSampel);
}

void matikanPompa() {
  if (statusPompa) {
    statusPompa = false;
    hitungTransisiRelai++;
  }
  digitalWrite(PIN_RELAY_POMPA, LOW);
}

void prosesTombol(unsigned long sekarang) {
  bool bacaTombol = digitalRead(PIN_TOMBOL_MODE);
  if (bacaTombol == LOW && tombolSebelumnya == HIGH &&
      (sekarang - waktuDebounceTombol > DEBOUNCE_MS)) {
    waktuDebounceTombol = sekarang;
    if (!faultTerkunci) {
      modeOverridePaksa = !modeOverridePaksa;
      Serial.printf("{\"event\":\"mode_change\",\"manual_override\":%s}\n",
                    modeOverridePaksa ? "true" : "false");
    } else {
      Serial.println("{\"event\":\"button_ignored\",\"reason\":\"fault_locked\"}");
    }
  }
  tombolSebelumnya = bacaTombol;
}

void evaluasiPompa(float soilPct, unsigned long sekarang) {
  bool pompaSebelum = statusPompa;

  if (faultTerkunci) {
    statusPompa = false;
  } else if (modeOverridePaksa) {
    statusPompa = true;
  } else {
#if GUNAKAN_HISTERESIS == 1
    if (soilPct < AMBANG_POMPA_ON) statusPompa = true;
    else if (soilPct >= AMBANG_POMPA_OFF) statusPompa = false;
#else
    statusPompa = (soilPct < AMBANG_TUNGGAL);
#endif
  }

  if (statusPompa != pompaSebelum) {
    hitungTransisiRelai++;
    if (statusPompa) {
      waktuMulaiPompa = sekarang;
      soilPctSaatPompaMulai = soilPct;
      soilPctTertinggiSaatPompa = soilPct;
    }
  }

  // C6: pompa mati dan fault terkunci jika 15 detik berjalan tanpa kenaikan kelembapan >= 1%.
  if (statusPompa) {
    if (soilPct > soilPctTertinggiSaatPompa) soilPctTertinggiSaatPompa = soilPct;
    if ((sekarang - waktuMulaiPompa >= BATAS_POMPA_MS) &&
        (soilPctTertinggiSaatPompa < soilPctSaatPompaMulai + KENAIKAN_MIN_SOIL_PCT)) {
      faultTerkunci = true;
      kodeFault = "DRY_RUN_PROTECTION";
      modeOverridePaksa = false;
      statusPompa = false;
      hitungTransisiRelai++;
      Serial.println("{\"event\":\"fault\",\"fault\":\"DRY_RUN_PROTECTION\"}");
    }
  }

  digitalWrite(PIN_RELAY_POMPA, statusPompa ? HIGH : LOW);
}

void prosesPeringatan(unsigned long sekarang) {
  if (faultTerkunci) {
    if (sekarang - waktuBlinkTerakhir >= 250) {
      waktuBlinkTerakhir = sekarang;
      ledFaultAktif = !ledFaultAktif;
      digitalWrite(PIN_LED_FAULT, ledFaultAktif ? HIGH : LOW);
    }
  } else {
    digitalWrite(PIN_LED_FAULT, LOW);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SOIL_ADC1, INPUT);       // GPIO 34 input-only, tanpa INPUT_PULLUP
  pinMode(PIN_TOMBOL_MODE, INPUT_PULLUP);
  pinMode(PIN_RELAY_POMPA, OUTPUT);
  pinMode(PIN_LED_FAULT, OUTPUT);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_SOIL_ADC1, ADC_11db);
  setupPwmKipas();
  dht.begin();

  digitalWrite(PIN_RELAY_POMPA, LOW);
  digitalWrite(PIN_LED_FAULT, LOW);
  Serial.println("{\"event\":\"boot\",\"lab\":\"Lab-01 Greenhouse Kopi UNIB\"}");
}

void loop() {
  unsigned long sekarang = millis();
  prosesTombol(sekarang);
  prosesPeringatan(sekarang);

  if (sekarang - waktuBacaTerakhir >= INTERVAL_BACA_MS) {
    waktuBacaTerakhir = sekarang;

    int adcRaw = analogRead(PIN_SOIL_ADC1);
    uint32_t soilMv = bacaSoilMilliVoltsTerfilter();
    float soilPct = (float)(MV_TANAH_KERING - (int)soilMv) * 100.0f /
                    (float)(MV_TANAH_KERING - MV_TANAH_BASAH);
    soilPct = constrain(soilPct, 0.0f, 100.0f);

    float suhuUdara = dht.readTemperature();
    float lembapUdara = dht.readHumidity();
    if (isnan(suhuUdara) || isnan(lembapUdara)) {
      Serial.println("{\"error\":\"Pembacaan DHT22 gagal\"}");
      return;
    }

    evaluasiPompa(soilPct, sekarang);

    // 28 C = 0%, 38 C = 100%, menggunakan PWM LEDC 5 kHz 8-bit.
    int nilaiDuty = map((long)(suhuUdara * 10.0f), 280, 380, 0, 255);
    nilaiDuty = constrain(nilaiDuty, 0, 255);
    tulisPwmKipas((uint8_t)nilaiDuty);
    float fanPct = (nilaiDuty * 100.0f) / 255.0f;

    JsonDocument doc;
    doc["device_id"] = "esp32-kopi-gh01";
    doc["seq"] = ++seqPaket;
    doc["adc_raw"] = adcRaw;
    doc["soil_mv_avg"] = soilMv;
    doc["soil_pct"] = soilPct;
    doc["temp_c"] = suhuUdara;
    doc["air_hum_pct"] = lembapUdara;
    doc["pump_on"] = statusPompa;
    doc["relay_switches"] = hitungTransisiRelai;
    doc["fan_pwm_pct"] = fanPct;
    doc["ctrl_mode"] = modeOverridePaksa ? "MANUAL" :
#if GUNAKAN_HISTERESIS == 1
      "HYSTERESIS";
#else
      "SINGLE_THR";
#endif
    doc["fault"] = faultTerkunci ? kodeFault : nullptr;
    serializeJson(doc, Serial);
    Serial.println();
  }
}
