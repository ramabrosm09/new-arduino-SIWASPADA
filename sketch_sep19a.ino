#include <Adafruit_BME280.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>

// ============================================================
//                     KONFIGURASI WIFI
// ============================================================

const char *ssid = "USERNAME WIFI";
const char *password = "PASSWORD WIFI";

// Isi dengan token BOT TELEGRAM kamu
// Jangan bagikan token ini.
const char *botToken = "TOKEN BOT";

// Isi dengan Chat ID kamu
const char *chatID = "CHAT ID";

// ============================================================
//                     KONFIGURASI BME280
// ============================================================

Adafruit_BME280 bme;

#define SDA_PIN 21
#define SCL_PIN 22

const byte BME_ADDRESS = 0x76;

// ============================================================
//                     KONFIGURASI ANEMOMETER
// ============================================================

#define ANEMOMETER_PIN 14

// Sesuai kalibrasi anemometer
#define WIND_SPEED_18_PULSE_SECOND 2.0
#define ONE_ROTATION_SENSOR 18.0

// ============================================================
//                     INTERVAL SISTEM
// ============================================================

const unsigned long INTERVAL_SENSOR = 2000;
// BME280 dibaca setiap 2 detik

const unsigned long INTERVAL_ANEMOMETER = 1000;
// Anemometer dihitung setiap 1 detik

const unsigned long INTERVAL_ANALISIS = 600000;
// Analisis tren setiap 10 menit

const unsigned long INTERVAL_TELEGRAM = 5000;
// Telegram diperiksa setiap 2 detik

const unsigned long INTERVAL_WIFI = 15000;
// WiFi diperiksa setiap 15 detik

// ============================================================
//                     BATAS PERINGATAN
// ============================================================

// 12.5 m/s = 45 km/jam
// ================= THRESHOLD ANEMOMETER =================
const float WIND_NORMAL_MAX = 5.56;      // < 20 km/jam
const float WIND_WASPADA_MAX = 12.50;    // < 45 km/jam
const float BATAS_ANGIN_KENCANG = 12.50; // >= 45 km/jam
const float MAX_WIND_SENSOR = 50.00;     // batas validasi sensor

// ============================================================
//                     COOLDOWN ALERT
// ============================================================

const unsigned long COOLDOWN_ALERT = 300000;
// 5 menit

const unsigned long COOLDOWN_ANGIN = 300000;
// 5 menit

// ============================================================
//                     TIMER
// ============================================================

unsigned long waktuSensor = 0;
unsigned long waktuAnemometer = 0;
unsigned long waktuAnalisis = 0;
unsigned long waktuTelegram = 0;
unsigned long waktuWiFi = 0;

unsigned long waktuAlertTerakhir = 0;
unsigned long waktuAlertAnginTerakhir = 0;

// ============================================================
//                     DATA ANEMOMETER
// ============================================================

bool dataAnginValid = true;
volatile unsigned long pulseCount = 0;

unsigned long pulseSebelumnya = 0;
unsigned long pulsePerSecond = 0;

float kecepatanAngin = 0.0;
float kecepatanAnginKnot = 0.0;

// ============================================================
//                     DATA BME280
// ============================================================

float suhu = 0.0;
float kelembapan = 0.0;
float tekanan = 0.0;

// ============================================================
//                     DATA ANALISIS
// ============================================================

// Data 10 menit sebelumnya
float suhuAwal = 0.0;
float kelembapanAwal = 0.0;
float tekananAwal = 0.0;
float anginAwal = 0.0;

// Perubahan selama 10 menit
float deltaSuhu = 0.0;
float deltaTekanan = 0.0;
float deltaAngin = 0.0;

// Menandakan baseline sudah tersedia
bool dataAwalTersedia = false;

// ============================================================
//                     STATUS SIWASPADA
// ============================================================

String statusSIWASPADA = "NORMAL";

// Status sebelum analisis terbaru
String statusSebelumnya = "NORMAL";

int totalPoin = 0;

// ============================================================
//                     TELEGRAM
// ============================================================

long lastUpdateID = 0;

// ============================================================
//                     INTERRUPT ANEMOMETER
// ============================================================

volatile unsigned long lastPulseMicros = 0;

void IRAM_ATTR countPulse() {
  unsigned long now = micros();

  if (now - lastPulseMicros >= 5000) {
    pulseCount++;
    lastPulseMicros = now;
  }
}

// ============================================================
//                    Konfigurasi Spreadsheet
// ============================================================

const String spreadSheetLink =
    "https://docs.google.com/spreadsheets/d/"
    "1tolSgKivcggkRuuNLs9q1EF6Dg02HogH_e1DKQljavY/edit?usp=drivesdk";

// ============================================================
//                     URL TELEGRAM
// ============================================================

String telegramURL(String endpoint) {
  return "https://api.telegram.org/bot" + String(botToken) + "/" + endpoint;
}

// revisi bentar//

void tesKoneksiTelegram() {
  Serial.println();
  Serial.println("===== TES KONEKSI TELEGRAM =====");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TES] WiFi tidak terhubung.");
    return;
  }

  Serial.print("[TES] IP ESP32 : ");
  Serial.println(WiFi.localIP());

  Serial.print("[TES] RSSI      : ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10000);

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);

  String url = telegramURL("getMe");

  Serial.println("[TES] Mencoba koneksi ke Telegram...");

  if (!http.begin(client, url)) {
    Serial.println("[TES] HTTP begin GAGAL.");
    return;
  }

  int httpCode = http.GET();

  Serial.print("[TES] HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    Serial.println("[TES] Response:");
    Serial.println(http.getString());
  } else {
    Serial.print("[TES] Error: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();

  Serial.println("================================");
}

// ============================================================
//                     ESCAPE JSON
// ============================================================

String escapeJson(String teks) {
  teks.replace("\\", "\\\\");
  teks.replace("\"", "\\\"");
  teks.replace("\n", "\\n");
  teks.replace("\r", "\\r");

  return teks;
}

// ============================================================
//                     KIRIM TELEGRAM
// ============================================================

bool kirimTelegram(String pesan) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TELEGRAM] WiFi tidak terhubung.");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10000);

  HTTPClient http;

  http.setConnectTimeout(10000);
  http.setTimeout(10000);

  String url = telegramURL("sendMessage");

  Serial.println("[TELEGRAM] Mengirim pesan...");

  if (!http.begin(client, url)) {
    Serial.println("[TELEGRAM] HTTP begin gagal.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");

  String pesanAman = escapeJson(pesan);

  String payload =
      "{\"chat_id\":\"" + String(chatID) + "\",\"text\":\"" + pesanAman + "\"}";

  int httpCode = http.POST(payload);

  Serial.print("[TELEGRAM] HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String response = http.getString();

    Serial.print("[TELEGRAM] Response: ");
    Serial.println(response);
  } else {
    Serial.print("[TELEGRAM] Error: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();

  return httpCode == 200;
}

// ============================================================
//                     PESAN STATUS
// ============================================================

String buatPesanStatus() {
  String pesan = "";

  pesan += "SIWASPADA\n\n";

  pesan += "Kondisi Atmosfer\n\n";

  pesan += "Suhu            : ";
  pesan += String(suhu, 1);
  pesan += " C\n";

  pesan += "Kelembapan      : ";
  pesan += String(kelembapan, 1);
  pesan += " %\n";

  pesan += "Tekanan         : ";
  pesan += String(tekanan, 1);
  pesan += " hPa\n";

  pesan += "Kecepatan Angin : ";
  pesan += String(kecepatanAngin, 2);
  pesan += " m/s\n";

  pesan += "Kecepatan Angin : ";
  pesan += String(kecepatanAngin * 3.6, 1);
  pesan += " km/jam\n";

  pesan += "Pulse           : ";
  pesan += String(pulsePerSecond);
  pesan += " pulse/s\n\n";

  pesan += "Status          : ";
  pesan += statusSIWASPADA;
  pesan += "\n";

  pesan += "Poin Analisis   : ";
  pesan += String(totalPoin);

  return pesan;
}

// ============================================================
//                     PESAN ANALISIS
// ============================================================

String buatPesanAnalisis() {
  String pesan = "";

  pesan += "ANALISIS SIWASPADA\n\n";

  pesan += "Periode pengamatan: 10 menit\n\n";

  pesan += "Perubahan suhu    : ";
  pesan += String(deltaSuhu, 1);
  pesan += " C\n";

  pesan += "Perubahan tekanan : ";
  pesan += String(deltaTekanan, 1);
  pesan += " hPa\n";

  pesan += "Perubahan angin   : ";
  pesan += String(deltaAngin, 2);
  pesan += " m/s\n";

  pesan += "Kelembapan saat ini: ";
  pesan += String(kelembapan, 1);
  pesan += " %\n\n";

  pesan += "Total poin : ";
  pesan += String(totalPoin);
  pesan += "\n";

  pesan += "Status     : ";
  pesan += statusSIWASPADA;

  return pesan;
}

// ============================================================
//                     PESAN HELP
// ============================================================

String buatPesanHelp() {
  String pesan = "";

  pesan += "PERINTAH SIWASPADA\n\n";

  pesan += "/start\n";
  pesan += "Melihat informasi awal sistem.\n\n";

  pesan += "/status\n";
  pesan += "Melihat kondisi atmosfer terbaru.\n\n";

  pesan += "/analysis\n";
  pesan += "Melihat hasil analisis tren 10 menit.\n\n";

  pesan += "/help\n";
  pesan += "Menampilkan daftar perintah.";

  pesan += "/spreadsheet\n";
  pesan += "Menampilkan link spreadsheet.";

  return pesan;
}

// ============================================================
//              NOTIFIKASI PERUBAHAN STATUS
// ============================================================

void kirimNotifikasiPerubahanStatus() {
  String pesan = "";

  // ----------------------------------------------------------
  // NORMAL -> WASPADA
  // ----------------------------------------------------------

  if (statusSIWASPADA == "WASPADA") {
    pesan += "PERINGATAN SIWASPADA\n\n";

    pesan += "Status berubah menjadi: WASPADA\n\n";

    pesan += "Sistem mendeteksi perubahan "
             "parameter atmosfer yang perlu "
             "diperhatikan.\n\n";
  }

  // ----------------------------------------------------------
  // WASPADA -> SIAGA
  // ----------------------------------------------------------

  else if (statusSIWASPADA == "SIAGA") {
    pesan += "PERINGATAN SIWASPADA\n\n";

    pesan += "Status berubah menjadi: SIAGA\n\n";

    pesan += "Sistem mendeteksi kombinasi "
             "perubahan parameter atmosfer "
             "yang signifikan.\n\n";
  }

  // ----------------------------------------------------------
  // WASPADA/SIAGA -> NORMAL
  // ----------------------------------------------------------

  else if (statusSIWASPADA == "NORMAL") {
    pesan += "SIWASPADA\n\n";

    pesan += "Status kembali menjadi: NORMAL\n\n";

    pesan += "Parameter yang sebelumnya "
             "memicu peringatan telah berada "
             "di bawah batas status peringatan.\n\n";
  }

  // ----------------------------------------------------------
  // DATA TERKINI
  // ----------------------------------------------------------

  pesan += "Suhu        : ";
  pesan += String(suhu, 1);
  pesan += " C\n";

  pesan += "Kelembapan  : ";
  pesan += String(kelembapan, 1);
  pesan += " %\n";

  pesan += "Tekanan     : ";
  pesan += String(tekanan, 1);
  pesan += " hPa\n";

  pesan += "Angin       : ";
  pesan += String(kecepatanAngin, 2);
  pesan += " m/s\n\n";

  pesan += "Perubahan 10 menit:\n";

  pesan += "Δ Suhu     : ";
  pesan += String(deltaSuhu, 1);
  pesan += " C\n";

  pesan += "Δ Tekanan  : ";
  pesan += String(deltaTekanan, 1);
  pesan += " hPa\n";

  pesan += "Δ Angin    : ";
  pesan += String(deltaAngin, 2);
  pesan += " m/s\n\n";

  pesan += "Poin       : ";
  pesan += String(totalPoin);

  // ----------------------------------------------------------
  // KIRIM
  // ----------------------------------------------------------

  if (kirimTelegram(pesan)) {
    Serial.println("[STATUS] Notifikasi perubahan "
                   "status berhasil dikirim.");
  }
}

// ============================================================
//                     BACA ANEMOMETER
// ============================================================

void bacaAnemometer() {
  unsigned long pulseSekarang;

  noInterrupts();

  pulseSekarang = pulseCount;

  interrupts();

  pulsePerSecond = pulseSekarang - pulseSebelumnya;

  pulseSebelumnya = pulseSekarang;

  kecepatanAngin =
      (WIND_SPEED_18_PULSE_SECOND / ONE_ROTATION_SENSOR) * pulsePerSecond;

  // Validasi pembacaan anemometer
  if (kecepatanAngin > MAX_WIND_SENSOR) {
    dataAnginValid = false;

    Serial.println("[ANEMOMETER] Pembacaan tidak valid!");
    Serial.print("Kecepatan terbaca: ");
    Serial.print(kecepatanAngin, 2);
    Serial.println(" m/s");

    return;
  }

  dataAnginValid = true;

  kecepatanAnginKnot = kecepatanAngin / 0.514444;

  Serial.println();
  Serial.println("========== ANEMOMETER ==========");

  Serial.print("Pulse/detik     : ");

  Serial.println(pulsePerSecond);

  Serial.print("Kecepatan angin : ");

  Serial.print(kecepatanAngin, 2);

  Serial.println(" m/s");

  Serial.print("Kecepatan angin : ");

  Serial.print(kecepatanAngin * 3.6, 2);

  Serial.println(" km/jam");

  Serial.println("================================");
}

// ============================================================
//                     BACA BME280
// ============================================================

void bacaBME280() {
  suhu = bme.readTemperature();

  kelembapan = bme.readHumidity();

  tekanan = bme.readPressure() / 100.0F;

  Serial.println();
  Serial.println("============ BME280 ============");

  Serial.print("Suhu       : ");

  Serial.print(suhu, 2);

  Serial.println(" C");

  Serial.print("Kelembapan : ");

  Serial.print(kelembapan, 2);

  Serial.println(" %");

  Serial.print("Tekanan    : ");

  Serial.print(tekanan, 2);

  Serial.println(" hPa");

  Serial.println("=================================");
}

// ============================================================
//                ALERT ANGIN KENCANG LANGSUNG
// ============================================================

void cekAnginKencang() {

  if (!dataAnginValid) {
    Serial.println("[ANGIN] Data tidak valid, analisis dilewati.");
    return;
  }

  if (kecepatanAngin >= BATAS_ANGIN_KENCANG) {
    unsigned long sekarang = millis();

    if (sekarang - waktuAlertAnginTerakhir >= COOLDOWN_ANGIN) {
      String pesan = "";

      pesan += "PERINGATAN SIWASPADA\n\n";

      pesan += "Kecepatan angin terdeteksi tinggi.\n\n";

      pesan += "Kecepatan : ";

      pesan += String(kecepatanAngin, 2);

      pesan += " m/s\n";

      pesan += "Setara    : ";

      pesan += String(kecepatanAngin * 3.6, 1);

      pesan += " km/jam\n\n";

      pesan += "Sistem mendeteksi kondisi "
               "angin kencang yang perlu "
               "diwaspadai.\n\n";

      pesan += "Harap meningkatkan kewaspadaan "
               "dan memperhatikan informasi resmi.";

      if (kirimTelegram(pesan)) {
        waktuAlertAnginTerakhir = sekarang;

        Serial.println("[ALERT] Peringatan angin "
                       "kencang dikirim.");
      }
    }
  }
}

// ============================================================
//                  ANALISIS TREN 10 MENIT
// ============================================================

void analisisTrend() {

  if (!dataAwalTersedia) {
    suhuAwal = suhu;

    kelembapanAwal = kelembapan;

    tekananAwal = tekanan;

    anginAwal = kecepatanAngin;

    dataAwalTersedia = true;

    Serial.println("[ANALISIS] Data awal disimpan.");

    return;
  }

  deltaSuhu = suhu - suhuAwal;

  deltaTekanan = tekanan - tekananAwal;

  deltaAngin = kecepatanAngin - anginAwal;

  statusSebelumnya = statusSIWASPADA;

  // Reset poin
  totalPoin = 0;

  // ==========================================================
  // SUHU
  // ==========================================================

  if (deltaSuhu <= -4.0) {
    totalPoin += 2;
  }

  else if (deltaSuhu <= -2.0) {
    totalPoin += 1;
  }

  // ==========================================================
  // TEKANAN
  // ==========================================================

  if (deltaTekanan <= -3.0) {
    totalPoin += 2;
  }

  else if (deltaTekanan <= -1.5) {
    totalPoin += 1;
  }

  // ==========================================================
  // KELEMBAPAN
  // ==========================================================

  if (kelembapan > 90.0) {
    totalPoin += 2;
  }

  else if (kelembapan >= 81.0) {
    totalPoin += 1;
  }

  // ==========================================================
  // KECEPATAN ANGIN
  // ==========================================================

  if (deltaAngin >= 5.0) {
    totalPoin += 2;
  }

  else if (deltaAngin >= 3.0) {
    totalPoin += 1;
  }

  // ==========================================================
  // TENTUKAN STATUS BARU
  // ==========================================================

  if (totalPoin >= 5) {
    statusSIWASPADA = "SIAGA";
  }

  else if (totalPoin >= 2) {
    statusSIWASPADA = "WASPADA";
  }

  else {
    statusSIWASPADA = "NORMAL";
  }

  // ==========================================================
  // SERIAL MONITOR
  // ==========================================================

  Serial.println();
  Serial.println("=================================");

  Serial.println("       ANALISIS TREN 10 MENIT");

  Serial.println("=================================");

  Serial.print("Delta suhu      : ");

  Serial.print(deltaSuhu, 2);

  Serial.println(" C");

  Serial.print("Delta tekanan   : ");

  Serial.print(deltaTekanan, 2);

  Serial.println(" hPa");

  Serial.print("Delta angin     : ");

  Serial.print(deltaAngin, 2);

  Serial.println(" m/s");

  Serial.print("Kelembapan      : ");

  Serial.print(kelembapan, 2);

  Serial.println(" %");

  Serial.print("Total poin      : ");

  Serial.println(totalPoin);

  Serial.print("Status sebelum  : ");

  Serial.println(statusSebelumnya);

  Serial.print("Status sekarang : ");

  Serial.println(statusSIWASPADA);

  Serial.println("=================================");

  // ==========================================================
  // JIKA STATUS BERUBAH
  // ==========================================================

  if (statusSIWASPADA != statusSebelumnya) {
    unsigned long sekarang = millis();

    if (sekarang - waktuAlertTerakhir >= COOLDOWN_ALERT) {
      kirimNotifikasiPerubahanStatus();

      waktuAlertTerakhir = sekarang;
    }

    else {
      Serial.println("[STATUS] Status berubah, "
                     "tetapi alert masih cooldown.");
    }
  }

  // ==========================================================
  // SIMPAN DATA TERBARU
  // SEBAGAI BASELINE BERIKUTNYA
  // ==========================================================

  suhuAwal = suhu;

  kelembapanAwal = kelembapan;

  tekananAwal = tekanan;

  anginAwal = kecepatanAngin;
}

// ============================================================
//                     CEK TELEGRAM
// ============================================================

void cekTelegram() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  WiFiClientSecure client;

  client.setInsecure();

  HTTPClient http;

  http.setConnectTimeout(10000);
  http.setTimeout(10000);

  String url = telegramURL("getUpdates") +
               "?offset=" + String(lastUpdateID + 1) + "&limit=5&timeout=0";

  if (!http.begin(client, url)) {
    Serial.println("[TELEGRAM] HTTP begin gagal.");

    return;
  }

  int httpCode = http.GET();

  if (httpCode == 200) {
    String response = http.getString();

    // ========================================================
    // UPDATE ID
    // ========================================================

    int posisiUpdate = response.indexOf("\"update_id\":");

    if (posisiUpdate >= 0) {
      int awalID = posisiUpdate + 12;

      int akhirID = response.indexOf(",", awalID);

      if (akhirID > awalID) {
        long updateID = response.substring(awalID, akhirID).toInt();

        if (updateID > lastUpdateID) {
          lastUpdateID = updateID;
        }
      }
    }

    // ========================================================
    // /status
    // ========================================================

    if (response.indexOf("/status") >= 0) {
      Serial.println("[TELEGRAM] /status diterima.");

      kirimTelegram(buatPesanStatus());
    }

    // ========================================================
    // /analysis
    // ========================================================

    else if (response.indexOf("/analysis") >= 0) {
      Serial.println("[TELEGRAM] /analysis diterima.");

      kirimTelegram(buatPesanAnalisis());
    }

    // ========================================================
    // /help
    // ========================================================

    else if (response.indexOf("/help") >= 0) {
      Serial.println("[TELEGRAM] /help diterima.");

      kirimTelegram(buatPesanHelp());
    }

    // ========================================================
    // /start
    // ========================================================

    else if (response.indexOf("/start") >= 0) {
      Serial.println("[TELEGRAM] /start diterima.");

      String pesan = "";

      pesan += "SIWASPADA\n\n";

      pesan += "Sistem pemantauan atmosfer "
               "berhasil terhubung.\n\n";

      pesan += "Gunakan perintah:\n";

      pesan += "/status - kondisi terbaru\n";

      pesan += "/analysis - analisis tren 10 menit\n";

      pesan += "/help - daftar perintah";

      kirimTelegram(pesan);
    }

    else if (response.indexOf("/spreadsheet") >= 0) {
      kirimTelegram(spreadSheetLink);
    }
  }

  else {
    Serial.print("[TELEGRAM] HTTP Error: ");

    Serial.println(httpCode);
  }

  http.end();
}

// ============================================================
//                     CEK WIFI
// ============================================================

void cekWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.println("[WIFI] WiFi terputus.");

  Serial.println("[WIFI] Mencoba koneksi ulang...");

  // Memulai ulang koneksi WiFi
  // tanpa mengubah proses sensor.
  WiFi.disconnect();

  delay(100);

  WiFi.begin(ssid, password);
}

// ============================================================
//                     SETUP
// ============================================================

void setup() {
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("=================================");

  Serial.println("       SIWASPADA STARTING");

  Serial.println("=================================");

  // ==========================================================
  // BME280
  // ==========================================================

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!bme.begin(BME_ADDRESS)) {
    Serial.println("ERROR: BME280 tidak terdeteksi!");

    while (true) {
      delay(1000);
    }
  }

  Serial.println("BME280 berhasil terdeteksi.");

  // ==========================================================
  // ANEMOMETER
  // ==========================================================

  pinMode(ANEMOMETER_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ANEMOMETER_PIN), countPulse, FALLING);

  Serial.println("Anemometer siap.");

  // ==========================================================
  // WIFI
  // ==========================================================

  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, password);

  Serial.print("Menghubungkan WiFi");

  unsigned long mulaiWiFi = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - mulaiWiFi < 20000) {
    delay(500);

    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi TERHUBUNG. IP: ");

    Serial.println(WiFi.localIP());

    // Tes koneksi Telegram terlebih dahulu
    tesKoneksiTelegram();

    // Kirim pesan awal setelah tes
    kirimTelegram("SIWASPADA\n\n"
                  "Sistem berhasil terhubung "
                  "dan siap melakukan pemantauan.");
  }

  else {
    Serial.println("WiFi belum terhubung.");
  }

  // ==========================================================
  // TIMER AWAL
  // ==========================================================

  unsigned long sekarang = millis();

  waktuSensor = sekarang;

  waktuAnemometer = sekarang;

  waktuAnalisis = sekarang;

  waktuTelegram = sekarang;

  waktuWiFi = sekarang;

  Serial.println();

  Serial.println("Sistem SIWASPADA siap.");
}

// ============================================================
//                     LOOP UTAMA
// ============================================================

void loop() {
  unsigned long sekarang = millis();

  // ==========================================================
  // WIFI
  // ==========================================================

  if (sekarang - waktuWiFi >= INTERVAL_WIFI) {
    cekWiFi();

    waktuWiFi = sekarang;
  }

  // ==========================================================
  // ANEMOMETER
  // ==========================================================

  if (sekarang - waktuAnemometer >= INTERVAL_ANEMOMETER) {
    bacaAnemometer();

    // Cek angin ekstrem secara langsung
    cekAnginKencang();

    waktuAnemometer = sekarang;
  }

  // ==========================================================
  // BME280
  // ==========================================================

  if (sekarang - waktuSensor >= INTERVAL_SENSOR) {
    bacaBME280();

    waktuSensor = sekarang;
  }

  // ==========================================================
  // DATA AWAL
  // ==========================================================

  if (!dataAwalTersedia) {
    static unsigned long waktuDataAwal = 0;

    if (waktuDataAwal == 0) {
      waktuDataAwal = sekarang;
    }

    bool dataValid = !isnan(suhu) && !isnan(kelembapan) && !isnan(tekanan);

    if (dataValid && sekarang - waktuDataAwal >= 3000) {
      suhuAwal = suhu;

      kelembapanAwal = kelembapan;

      tekananAwal = tekanan;

      anginAwal = kecepatanAngin;

      dataAwalTersedia = true;

      waktuAnalisis = sekarang;

      Serial.println();

      Serial.println("[ANALISIS] Data awal disimpan.");

      Serial.println("[ANALISIS] Pengamatan 10 menit dimulai.");
    }
  }

  // ==========================================================
  // ANALISIS 10 MENIT
  // ==========================================================

  if (dataAwalTersedia && sekarang - waktuAnalisis >= INTERVAL_ANALISIS) {
    analisisTrend();

    waktuAnalisis = sekarang;
  }

  // ==========================================================
  // TELEGRAM
  // ==========================================================

  if (sekarang - waktuTelegram >= INTERVAL_TELEGRAM) {
    cekTelegram();

    waktuTelegram = sekarang;
  }

  // ==========================================================
  // YIELD
  // ==========================================================

  yield();
}