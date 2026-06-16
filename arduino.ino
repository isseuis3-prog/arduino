#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>       // Menggunakan EEPROM agar 100% sukses di compiler situs GitHub
#include <NimBLEDevice.h> // Menggunakan NimBLE agar scan Bluetooth super cepat dan responsif

// Pengaturan Pin ESP32-C3 Super Mini
const int pinKontak  = 4;  // Hubungkan ke modul relay kontak
const int pinBuzzer  = 5;  // Hubungkan ke buzzer aktif
const int pinSDA     = 10; // Pin SDA modul RFID
const int pinRST     = 3;  // Pin RST modul RFID

MFRC522 rfid(pinSDA, pinRST);

// Konfigurasi Fitur Bluetooth (Remot Tomsis)
String alamatBLeTomsis = "AA:BB:CC:DD:EE:FF"; // MASUKKAN MAC ADDRESS TOMSIS ANDA DI SINI
const int ambatBatasRSSI = -75;                // Disetel -75 agar merespon di jarak sekitar 3 meter
bool tomsisSudahTrigger = false;               // Variabel pengunci agar Tomsis hanya berfungsi 1 kali di awal

bool statusKontak = false;
bool modeBelajar = false;
unsigned long waktuModeBelajar = 0;

bool kartuSudahTerlepas = true;
unsigned long waktuTerakhirMembaca = 0;

byte uidMaster[4] = {0, 0, 0, 0};
byte uidAnakan[4] = {0, 0, 0, 0};

// Fungsi Nada Buzzer Menyalakan Kontak (BIP BIP - Cepat & Modern)
void suaraKontakOn() {
  digitalWrite(pinBuzzer, HIGH); delay(80); 
  digitalWrite(pinBuzzer, LOW);  delay(80); 
  digitalWrite(pinBuzzer, HIGH); delay(80); 
  digitalWrite(pinBuzzer, LOW);
}

// Fungsi Nada Buzzer Mematikan Kontak (BIP Sekali - Agak Panjang)
void suaraKontakOff() {
  digitalWrite(pinBuzzer, HIGH); delay(180); 
  digitalWrite(pinBuzzer, LOW);
}

// Fungsi Nada Indikator Sistem / Mode Belajar
void bipPendek(int jumlah) {
  for(int i=0; i<jumlah; i++){
    digitalWrite(pinBuzzer, HIGH); delay(120);
    digitalWrite(pinBuzzer, LOW);  delay(120);
    yield();
  }
}

void bipPanjang() {
  digitalWrite(pinBuzzer, HIGH); delay(800);
  digitalWrite(pinBuzzer, LOW);
}

// Fungsi Mengubah Status Relay dan Buzzer
void toggleKontakRFID() {
  if (!statusKontak) {
    statusKontak = true;
    digitalWrite(pinKontak, HIGH); // Relay Menyala
    suaraKontakOn();                // Bunyi BIP BIP
  } else {
    statusKontak = false;
    digitalWrite(pinKontak, LOW);  // Relay Mati
    suaraKontakOff();               // Bunyi BIP Sekali
    tomsisSudahTrigger = false;     // Reset pemicu Bluetooth saat dimatikan manual via kartu
  }
}

// Class Callback untuk membaca sinyal Bluetooth sekitar menggunakan NimBLE
class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        // Jika sudah pernah dipicu Bluetooth atau kontak menyala manual, abaikan pemindaian selanjutnya
        if (tomsisSudahTrigger || statusKontak) return;

        if (advertisedDevice->getAddress().toString() == alamatBLeTomsis.c_str()) {
            if (advertisedDevice->getRSSI() >= ambatBatasRSSI) {
                // Tomsis terdeteksi di dalam jarak ~3 meter, aktifkan relay pertama kali
                statusKontak = true;
                digitalWrite(pinKontak, HIGH);
                suaraKontakOn();
                tomsisSudahTrigger = true; // Kunci sistem: Tomsis tidak akan mematikan relay lagi
            }
        }
    }
};

void setup() {
  // 1. Inisialisasi Pin Output
  digitalWrite(pinKontak, LOW);  
  digitalWrite(pinBuzzer, LOW);  
  pinMode(pinBuzzer, OUTPUT); 
  pinMode(pinKontak, OUTPUT); 
  
  // 2. Inisialisasi Memori EEPROM Emulator internal ESP32
  EEPROM.begin(512);
  SPI.begin(6, 2, 7, 10); // Jalur hardware SPI ESP32-C3: SCK=6, MISO=2, MOSI=7, SS=10
  rfid.PCD_Init();   

  // 3. Penguat Antena Standar (38 dB) agar aman saat menempel erat pada bodi motor
  rfid.PCD_SetAntennaGain(rfid.RxGain_38dB);

  // 4. Read data kartu dari EEPROM simulasian
  for (int i = 0; i < 4; i++) uidMaster[i] = EEPROM.read(200 + i);
  for (int i = 0; i < 4; i++) uidAnakan[i] = EEPROM.read(204 + i);

  // 5. Inisialisasi Bluetooth BLE internal via NimBLE
  NimBLEDevice::init("KunciMotor_C3");
  NimBLEScan* pNimBLEScan = NimBLEDevice::getScan();
  pNimBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pNimBLEScan->setActiveScan(true);
  pNimBLEScan->setInterval(40);     // Setelan agresif: Memindai ulang tiap 40ms
  pNimBLEScan->setWindow(30);        // Setelan agresif: Membuka jendela baca 30ms agar respon instan
}

void loop() {
  // 1. PROSES PEMBACAAN REMOT BLUETOOTH TOMSIS (Hanya berjalan jika motor mati dan belum ter-trigger)
  if (!tomsisSudahTrigger && !statusKontak) {
    NimBLEDevice::getScan()->start(1, false); // Scan perangkat sekitar selama 1 detik
  }

  // 2. PROSES PEMBACAAN RFID KARTU (Selalu Aktif Bersamaan)
  if (modeBelajar && (millis() - waktuModeBelajar > 10000)) {
    modeBelajar = false;
    bipPendek(3); // Bunyi BIP 3 kali tanda keluar mode belajar
  }
  
  static unsigned long waktuSiklusRFID = 0;
  if (millis() - waktuSiklusRFID > 100) { 
    waktuSiklusRFID = millis();
    
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      waktuTerakhirMembaca = millis();
      
      if (kartuSudahTerlepas) {
        kartuSudahTerlepas = false; 
        
        // Pendaftaran Kartu MASTER jika memori kosong (Pertama kali alat dinyalakan)
        if (uidMaster[0] == 0 && uidMaster[1] == 0 && uidMaster[2] == 0 && uidMaster[3] == 0) {
          for (int i = 0; i < 4; i++) {
            uidMaster[i] = rfid.uid.uidByte[i];
            EEPROM.write(200 + i, uidMaster[i]);
          }
          EEPROM.commit();
          bipPanjang(); // Bunyi panjang tanda sukses mendaftarkan kartu Master
        } 
        else {
          // Cek apakah kartu yang di-tap adalah kartu Master
          bool isMaster = true;
          for (int i = 0; i < 4; i++) {
            if (rfid.uid.uidByte[i] != uidMaster[i]) { isMaster = false; break; }
          }
          
          if (isMaster) {
            modeBelajar = !modeBelajar; 
            if (modeBelajar) {
              waktuModeBelajar = millis();
              bipPanjang(); // Bunyi panjang masuk mode belajar kartu anakan
            } else {
              bipPendek(2); // Bunyi 2 kali keluar mode belajar
            }
          } 
          else {
            // PROSES DI DALAM MODE BELAJAR (Mendaftarkan/Menghapus Kartu Anakan)
            if (modeBelajar) {
              bool isSudahAnakan = true;
              for (int i = 0; i < 4; i++) {
                if (rfid.uid.uidByte[i] != uidAnakan[i]) { isSudahAnakan = false; break; }
              }
              
              if (isSudahAnakan) {
                // Jika kartu anakan yang sama di-tap lagi saat mode belajar, hapus kartu tersebut
                for (int i = 0; i < 4; i++) {
                  uidAnakan[i] = 0;
                  EEPROM.write(204 + i, 0);
                }
                EEPROM.commit();
                modeBelajar = false; 
                bipPendek(1); delay(100); bipPanjang(); 
              } 
              else {
                // Daftarkan kartu anakan baru ke EEPROM
                for (int i = 0; i < 4; i++) {
                  uidAnakan[i] = rfid.uid.uidByte[i];
                  EEPROM.write(204 + i, uidAnakan[i]);
                }
                EEPROM.commit();
                modeBelajar = false; 
                bipPendek(5); // Bunyi 5 kali tanda sukses simpan kartu anakan baru
              }
            } 
            else {
              // MODE OPERASIONAL (Tap Kartu Anakan untuk ON / OFF Relay)
              bool isAnakan = true;
              for (int i = 0; i < 4; i++) {
                if (rfid.uid.uidByte[i] != uidAnakan[i]) { isAnakan = false; break; }
              }
              
              if (isAnakan && uidAnakan[0] != 0) {
                toggleKontakRFID(); // Menyalakan atau mematikan relay + suara khas
              } else {
                bipPendek(1); // Bunyi sekali jika kartu salah / tidak terdaftar
              }
            }
          }
        }
      }
      rfid.PICC_HaltA();
    }
  }
  
  if (!kartuSudahTerlepas && (millis() - waktuTerakhirMembaca > 400)) {
    kartuSudahTerlepas = true;
  }
  
  yield(); 
}
