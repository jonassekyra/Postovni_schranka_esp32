#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_NeoPixel.h>
#include <ESP32Servo.h>       
#include <driver/rtc_io.h>    

// --- ÚDAJE PRO WI-FI A TELEGRAM ---
const char* ssid = "TVOJE_WIFI_JMENO";
const char* password = "TVOJE_WIFI_HESLO";
#define BOTtoken "TVOJ_TELEGRAM_BOT_TOKEN"
#define CHAT_ID "TVOJE_CHAT_ID"

// --- NASTAVENÍ SCHRÁNKY ---
const float ZAKLADNI_HLOUBKA = 20.0; // cm (vzdálenost prázdné schránky)
const float TOLERANCE_HLOUBKY = 2.0; // cm (ignoruje odchylky měření)

// --- PINY ---
const int senzorPin = 13; 
const int spinacPin = 33; 
const int trigPin = 5;    
const int echoPin = 4;    
#define RST_PIN 22        
#define SS_PIN 21         
const int ledPin = 27;    
#define NUMPIXELS 1       
const int servoPin = 32;  

// --- ÚHLY SERVA ---
const int UHEL_ZAMCENO = 100; 
const int UHEL_ODEMCENO = 10;

#define BITMASK_SPINAC (1ULL << 33)

// --- OBJEKTY ---
MFRC522 mfrc522(SS_PIN, RST_PIN);
WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);
Adafruit_NeoPixel pixels(NUMPIXELS, ledPin, NEO_GRB + NEO_KHZ800);
Servo mujZamek;           

void setup() {
  Serial.begin(115200);
  pinMode(senzorPin, INPUT);
  pinMode(spinacPin, INPUT_PULLUP); 
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  
  pixels.begin();
  pixels.clear();
  pixels.show();

  ESP32PWM::allocateTimer(0);
  mujZamek.setPeriodHertz(50);
  mujZamek.attach(servoPin);
  mujZamek.write(UHEL_ZAMCENO); 

  SPI.begin();
  mfrc522.PCD_Init();

  esp_sleep_wakeup_cause_t duvod = esp_sleep_get_wakeup_cause();

  if (duvod == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("Probuzeno IR senzorem! Jdu overit vzdalenost...");
    zpracujPostu();
  }
  else if (duvod == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.println("Klapka zvednuta! Hlidam karty, tukani i padajici dopisy...");
    
    pixels.setPixelColor(0, pixels.Color(0, 0, 50)); 
    pixels.show();
    
    unsigned long wakeTimer = millis();
    int pocetTuknuti = 1; 
    int posledniStavSpinace = HIGH; 
    
    bool podezreniNaDopis = false;
    bool majitelOtevrel = false;

    while (millis() - wakeTimer < 15000) {
      
      // 1. ZAZNAMENÁNÍ POHYBU (IR Senzor)
      if (digitalRead(senzorPin) == LOW && !podezreniNaDopis) {
        Serial.println("Neco se pohnulo u IR senzoru! Znamenam si to...");
        podezreniNaDopis = true;
        pixels.setPixelColor(0, pixels.Color(100, 0, 100)); 
        pixels.show(); delay(50);
        pixels.setPixelColor(0, pixels.Color(0, 0, 50)); 
        pixels.show();
      }

      // 2. HLÍDÁNÍ NOUZOVÉHO ŤUKÁNÍ
      int aktualniStav = digitalRead(spinacPin);
      if (aktualniStav == HIGH && posledniStavSpinace == LOW) {
        delay(50); 
        pocetTuknuti++;
        
        pixels.setPixelColor(0, pixels.Color(255, 255, 0)); 
        pixels.show(); delay(100);
        pixels.setPixelColor(0, pixels.Color(0, 0, 50)); 
        pixels.show();

        if (pocetTuknuti >= 3) {
          Serial.println("🔓 TAJNY KOD! Jsi to ty, šéfe.");
          majitelOtevrel = true; 
          odemkniAZasliZpravu("🔓 Schránka otevřena nouzovým ťukáním!");
          break; 
        }
      }
      posledniStavSpinace = aktualniStav;

      // 3. HLÍDÁNÍ NFC KARTY
      if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        String nacteneUID = "";
        for (byte i = 0; i < mfrc522.uid.size; i++) {
          nacteneUID += String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
          nacteneUID += String(mfrc522.uid.uidByte[i], HEX);
        }
        nacteneUID.trim(); nacteneUID.toUpperCase();
        
        if (nacteneUID == "79 9E 65 11" || nacteneUID == "67 F5 24 15") {
          Serial.println("✅ SPRAVNA KARTA! Jsi to ty, šéfe.");
          majitelOtevrel = true; 
          odemkniAZasliZpravu("✅ Schránka úspěšně odemčena čipem!");
        } else {
          Serial.println("❌ Cizí karta!");
          pixels.setPixelColor(0, pixels.Color(255, 0, 0)); 
          pixels.show();
          delay(2000); 
        }
        break; 
      }
    }

    // VYHODNOCENÍ PO ZAVŘENÍ
    Serial.println("Cekam na zavreni klapky...");
    while(digitalRead(spinacPin) == HIGH) {
      pixels.setPixelColor(0, pixels.Color(50, 0, 0)); 
      pixels.show();
      delay(500);
    }
    
    if (podezreniNaDopis && !majitelOtevrel) {
      Serial.println("Klapka zavrena, majitel se neukazal -> Jdu zmerit vzdalenost!");
      zpracujPostu();
    }
  }

  // USPÁVÁNÍ
  pixels.clear();
  pixels.show();
  delay(100); 
  
  rtc_gpio_pullup_en(GPIO_NUM_33);
  rtc_gpio_pulldown_dis(GPIO_NUM_33);

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 0); 
  esp_sleep_enable_ext1_wakeup(BITMASK_SPINAC, ESP_EXT1_WAKEUP_ANY_HIGH); 
  
  Serial.println("Zavreno. Jdu spat.");
  esp_deep_sleep_start();
}

void loop() {}

// POMOCNÉ FUNKCE
void zpracujPostu() {
  pixels.setPixelColor(0, pixels.Color(0, 0, 255)); 
  pixels.show();
  
  // Změření vzdálenosti
  digitalWrite(trigPin, LOW); delayMicroseconds(5);
  digitalWrite(trigPin, HIGH); delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  float vzdalenost = duration * 0.034 / 2;

  Serial.print("Namerena vzdalenost: ");
  Serial.print(vzdalenost);
  Serial.println(" cm");

  // DVOJITÁ KONTROLA: Je tam opravdu dopis? (Vzdálenost je menší než 20 cm mínus tolerance)
  if (duration > 0 && vzdalenost < (ZAKLADNI_HLOUBKA - TOLERANCE_HLOUBKY)) {
    Serial.println("Potvrzeno! Vzdalenost se zmensila. Posilam Telegram.");

    WiFi.begin(ssid, password);
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) { delay(500); }

    if (WiFi.status() == WL_CONNECTED) {
      client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
      String zprava = "📬 MÁŠ POŠTU! \n";
      zprava += "Něco přibylo. Volné místo ke dnu: " + String(vzdalenost) + " cm.";
      bot.sendMessage(CHAT_ID, zprava, "");
    }
  } else {
    Serial.println("Falesny poplach! Vzdalenost se nezmenila. Zpravu neposilam.");
  }
}

void odemkniAZasliZpravu(String textZpravy) {
  pixels.setPixelColor(0, pixels.Color(255, 255, 255)); 
  pixels.show();
  
  mujZamek.write(UHEL_ODEMCENO); 

  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 8000) { delay(100); }
  
  if (WiFi.status() == WL_CONNECTED) {
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
    bot.sendMessage(CHAT_ID, textZpravy, "");
  }

  delay(5000); 
  mujZamek.write(UHEL_ZAMCENO); 
}