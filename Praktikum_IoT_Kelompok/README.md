# Praktikum IoT Kelompok — Minggu 1 sampai 3

Setiap folder Minggu adalah project PlatformIO mandiri. Buka folder minggu yang sedang dikerjakan
(**File → Open Folder**) di VS Code, lalu gunakan PlatformIO: Build / Upload / Monitor.
`diagram.json` berada di root project agar dapat dibaca oleh ekstensi Wokwi.

## Isi
- `Minggu-1`: Lab-0 — DHT22, LED, pushbutton, blocking/non-blocking, JSON, dan C6 mode siaga 500 ms.
- `Minggu-2`: Lab-1 — ADC1 soil moisture, kalibrasi mV, moving average, histeresis, PWM kipas,
  override manual, telemetri JSON, dan C6 dry-run protection.
- `Minggu-3`: Lab-2 — I2C scanner, BMP280, OLED SSD1306, FreeRTOS dual-core, deep sleep,
  EXT0/timer wakeup, RTC_DATA_ATTR, dan C6 suhu ekstrem.

## Penting
1. Tiap project mempunyai `platformio.ini` sendiri. File ini diperlukan PlatformIO untuk menentukan
   board, framework, monitor speed, dan library.
2. Pilih `Minggu-1`, `Minggu-2`, atau `Minggu-3` sebagai folder yang dibuka di VS Code.
3. Simulasi Wokwi: jalankan ekstensi Wokwi pada folder minggu yang sama; ekstensi membaca `diagram.json`.
4. Nilai kalibrasi tanah pada Lab-1 (`2650 mV` kering dan `1100 mV` basah) mengikuti nilai awal modul.
   Untuk sensor fisik, ukur ulang kedua titik sesuai prosedur PDF.
5. Lab-2 menggunakan BMP280 karena komponen Wokwi pada modul adalah `wokwi-bmp280`.
   BMP280 tidak mengukur kelembapan udara; data yang tersedia adalah suhu, tekanan, dan elevasi.
6. Fitur C6 merupakan pengembangan di atas listing dasar modul. Uji pada simulator dan sesuaikan bila
   versi library/board lokal berbeda.

## Git kolaborasi
Ketua kelompok dapat membuat repository utama. Untuk tiap praktikum, anggota yang bertugas membuat
branch, menguji, lalu push. Contoh:
```bash
git switch main
git pull
git switch -c feat/lab01-sensor-actuator
git add Minggu-2
git commit -m "feat(lab-1): implementasi sensor dan aktuator greenhouse"
git push -u origin feat/lab01-sensor-actuator
```
Anggota berikutnya menggunakan nama branch sesuai minggu dan tugasnya. Merge melalui Pull Request
agar riwayat kontribusi tiap anggota terlihat.
