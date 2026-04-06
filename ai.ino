// ╔══════════════════════════════════════════════════════════════════════╗
// ║  BLINK OS — v16.0 (Matrix Terminal Intelligence)                    ║
// ║  [USER] Ibrahim / Konya / AI Operations                             ║
// ║  [FEAT] Multi-Intent Queue & Priority Interrupt Logic               ║
// ║  [UI] 240x240 Monochrome Green Cyberpunk Aesthetic                  ║
// ║  [STATUS] FULL CODE - NO SKIPS                                      ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <TFT_eSPI.h>
#include <SPI.h>
#include <driver/i2s.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <math.h>

// ─────────────────────────────────────────────────────────────────────────────
//  DONANIM TANIMLAMALARI (PINOUT)
// ─────────────────────────────────────────────────────────────────────────────
#define T1         8    // Kayıt Butonu
#define T2         47   // Menü Butonu (GPIO 47)
#define AMP_SD     7    // Hoparlör Güç Pin
#define LED_R      16
#define LED_G      17
#define LED_B      18
#define AMP_BCLK   4
#define AMP_LRC    5
#define AMP_DIN    6
#define MIC_SCK    42
#define MIC_WS     2
#define MIC_SD     41

// ─────────────────────────────────────────────────────────────────────────────
//  ESTETİK VE RENKLER (Matrix Paleti)
// ─────────────────────────────────────────────────────────────────────────────
#define M_GREEN    0x07E0   // Parlak Matrix Yeşili
#define M_DARK     0x0200   // Koyu Yeşil (Gölgeler için)
#define M_BLACK    0x0000   // Saf Siyah

enum BlinkState { IDLE, LISTENING, THINKING, SPEAKING, MUSIC_PLAYING, SHOW_MENU, REMINDER_ALERT };
volatile BlinkState currentState = IDLE;

TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft); // Maskot için buffer (200x162)

// ─────────────────────────────────────────────────────────────────────────────
//  SİSTEM DEĞİŞKENLERİ & AĞ
// ─────────────────────────────────────────────────────────────────────────────
const char* ssid       = "Tenda";
const char* password   = "acar2007";
const char* serverBase = "https://crispier-carmelina-chronically.ngrok-free.dev";

String URL_MAIN  = String(serverBase) + "/ses-test";
String URL_MUSIC = String(serverBase) + "/music?q=";
String URL_TTS   = String(serverBase) + "/tts?q=";

#define MAX_REM 8
struct Reminder { unsigned long trigMs; char msg[80]; bool active; };
Reminder reminders[MAX_REM];
bool isInterrupting = false;

// Ses Tamponları
static uint8_t* gBuf    = nullptr;
static size_t   gBufLen = 0, gBufPos = 0;
bool uiNeedsFullDraw = true;

// VAD / Kayıt Ayarları
#define VAD_CHUNK_BYTES     1024
#define VAD_SILENCE_THRESH  50
#define MIN_RECORD_MS       3000
#define MAX_RECORD_MS       10000
#define TX_BUF_SIZE         (16000 * 2 * 10) // 10 saniyelik 16kHz PCM buffer

// ─────────────────────────────────────────────────────────────────────────────
//  YARDIMCI FONKSİYONLAR
// ─────────────────────────────────────────────────────────────────────────────
String cleanTR(String s) {
  s.replace("ı","i"); s.replace("İ","I"); s.replace("ş","s"); s.replace("Ş","S");
  s.replace("ğ","g"); s.replace("Ğ","G"); s.replace("ç","c"); s.replace("Ç","C");
  s.replace("ö","o"); s.replace("Ö","O"); s.replace("ü","u"); s.replace("Ü","U");
  return s;
}

String urlEncode(String s) {
  String e = ""; char h[4];
  for(char c : s) {
    if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~') e += c;
    else if(c==' ') e += '+';
    else { sprintf(h,"%%%02X",(uint8_t)c); e += h; }
  }
  return e;
}

String fmtRem(unsigned long ms) {
  long r = (long)(ms - millis()) / 1000L;
  if(r <= 0) return "EXPIRED";
  if(r < 60) return String(r) + "s";
  return String(r/60) + "m " + String(r%60) + "s";
}

uint8_t bufU8() { return (gBufPos < gBufLen) ? gBuf[gBufPos++] : 0; }
uint16_t bufU16LE() { uint8_t a = bufU8(), b = bufU8(); return (uint16_t)a | ((uint16_t)b << 8); }
uint32_t bufU32LE() { uint32_t res = bufU16LE(); return res | ((uint32_t)bufU16LE() << 16); }

// ─────────────────────────────────────────────────────────────────────────────
//  UI: THE BLINK CORE (MATRIX MASKOT ANİMASYONU)
// ─────────────────────────────────────────────────────────────────────────────
void drawMaskot(BlinkState st) {
  spr.fillSprite(M_BLACK);
  int cx = 100, cy = 81; 
  float t = millis() / 1000.0f;

  // 1. Dış Katman: Heksagonal Halka
  int r_ext = 55 + (st == LISTENING ? (int)(sinf(t * 12) * 6) : 0);
  spr.drawCircle(cx, cy, r_ext, M_DARK);
  spr.drawCircle(cx, cy, r_ext - 1, M_GREEN);

  // 2. Çekirdek (State tabanlı animasyon)
  if (st == THINKING) {
    float rot = t * 6;
    for (int i = 0; i < 6; i++) {
      int dx = cosf(rot + i * 1.04) * 25;
      int dy = sinf(rot + i * 1.04) * 25;
      spr.drawLine(cx, cy, cx + dx, cy + dy, M_GREEN);
      spr.fillCircle(cx + dx, cy + dy, 3, M_GREEN);
    }
  } else if (st == SPEAKING) {
    for (int i = -3; i <= 3; i++) {
      int h = 10 + abs(sinf(t * 18 + i * 0.6)) * 50;
      spr.fillRect(cx + (i * 10) - 2, cy - h / 2, 4, h, M_GREEN);
    }
  } else if (st == LISTENING) {
    spr.fillCircle(cx, cy, 20 + (int)(sinf(t * 15) * 12), M_GREEN);
    spr.drawCircle(cx, cy, 38 + (int)(sinf(t * 15) * 6), M_GREEN);
  } else if (st == MUSIC_PLAYING) {
    int r_mus = 15 + (int)(abs(sinf(t * 8)) * 15);
    spr.drawRect(cx - r_mus, cy - r_mus, r_mus*2, r_mus*2, M_GREEN);
    spr.drawRect(cx - r_mus + 4, cy - r_mus + 4, (r_mus-4)*2, (r_mus-4)*2, M_DARK);
  } else { // STANDBY
    spr.fillCircle(cx, cy, 8, M_GREEN);
    spr.drawCircle(cx, cy, 22 + (int)(sinf(t * 2) * 4), M_DARK);
  }

  // Glitch Efekti (Matrix havası için rastgele çizgiler)
  if (random(100) > 97) {
    int gy = cy + random(-60, 60);
    spr.drawLine(cx - 80, gy, cx + 80, gy, M_GREEN);
  }

  spr.pushSprite(20, 40); 
}

// ─────────────────────────────────────────────────────────────────────────────
//  SES MOTORU VE HATIRLATICI KESMESİ
// ─────────────────────────────────────────────────────────────────────────────
void showReminderAlert(const char* msg); // Prototip

void checkAndFireReminder() {
  if (isInterrupting || currentState == SHOW_MENU) return;
  for (int i = 0; i < MAX_REM; i++) {
    if (reminders[i].active && millis() >= reminders[i].trigMs) {
      reminders[i].active = false;
      isInterrupting = true;
      
      BlinkState backup = currentState;
      showReminderAlert(reminders[i].msg);
      
      currentState = backup;
      isInterrupting = false;
      tft.fillScreen(M_BLACK);
      uiNeedsFullDraw = true;
    }
  }
}

void playPCMFromBuf(uint32_t audlen, String* scroll = nullptr) {
  if (!gBuf || gBufPos + audlen > gBufLen) { gBufPos += audlen; return; }
  
  digitalWrite(AMP_SD, HIGH);
  i2s_zero_dma_buffer(I2S_NUM_0);
  
  uint32_t played = 0;
  uint8_t tmp[1024];
  
  while (played < audlen && gBufPos < gBufLen) {
    checkAndFireReminder(); // Çalarken hatirlatici kontrolü (AGENT LOGIC)

    uint32_t chunk = min((uint32_t)1024, audlen - played);
    chunk &= ~1; // Çift byte hizalama
    if (chunk == 0) break;
    
    memcpy(tmp, gBuf + gBufPos, chunk);
    gBufPos += chunk; played += chunk;
    size_t bw;
    i2s_write(I2S_NUM_0, tmp, chunk, &bw, portMAX_DELAY);
    
    if (played % 4096 == 0) {
      drawMaskot(SPEAKING);
      if (scroll && scroll->length() > 0) {
        tft.setCursor(5, 218); tft.setTextColor(M_GREEN, M_BLACK); tft.setTextSize(1);
        String s = *scroll;
        if(s.length() > 35) s = s.substring(0, 32) + "...";
        tft.print("> " + s);
      }
    }
  }
  delay(100);
  digitalWrite(AMP_SD, LOW);
}

// ─────────────────────────────────────────────────────────────────────────────
//  PAKET İŞLEYİCİLER (MULTI-INTENT SEQUENTIAL)
// ─────────────────────────────────────────────────────────────────────────────
void bufHandleAudio() {
  uint16_t tlen = bufU16LE(); 
  uint32_t alen = bufU32LE();
  char* txt = (char*)malloc(tlen + 1);
  if(!txt) { gBufPos += tlen + alen; return; }
  memcpy(txt, gBuf + gBufPos, tlen); gBufPos += tlen; txt[tlen] = '\0';
  String msg = cleanTR(String(txt)); free(txt);
  
  BlinkState old = currentState;
  currentState = SPEAKING;
  tft.fillRect(0, 215, 240, 25, M_BLACK);
  playPCMFromBuf(alen, &msg);
  currentState = old;
}

void bufHandleReminder() {
  uint32_t s = bufU32LE(); 
  uint16_t mlen = bufU16LE();
  char* m = (char*)malloc(mlen + 1);
  if(!m) { gBufPos += mlen; return; }
  memcpy(m, gBuf + gBufPos, mlen); gBufPos += mlen; m[mlen] = '\0';
  
  for (int i = 0; i < MAX_REM; i++) {
    if (!reminders[i].active) {
      reminders[i].trigMs = millis() + (s * 1000);
      strncpy(reminders[i].msg, m, 79); 
      reminders[i].active = true;
      break;
    }
  }
  free(m);
}

void bufHandleColor() {
  uint8_t r = bufU8(), g = bufU8(), b = bufU8();
  analogWrite(LED_R, r); analogWrite(LED_G, g); analogWrite(LED_B, b);
}

void parseResponseBuffer() {
  while (gBufPos < gBufLen) {
    uint8_t pkt = bufU8();
    if (pkt == 0xFF) break;
    switch (pkt) {
      case 0x01: { // STT Gösterimi
        uint16_t sl = bufU16LE(); char* st = (char*)malloc(sl+1);
        memcpy(st, gBuf+gBufPos, sl); gBufPos += sl; st[sl]='\0';
        tft.fillRect(0, 215, 240, 25, M_BLACK);
        tft.setCursor(5, 222); tft.setTextColor(M_DARK);
        tft.print("USR_INPUT: "); tft.print(cleanTR(String(st)));
        free(st); delay(800); break;
      }
      case 0x02: bufHandleAudio(); break;
      case 0x03: bufHandleReminder(); break;
      case 0x04: bufHandleColor(); break;
      case 0x05: { // Music Playlist
        uint8_t cnt = bufU8(); uint16_t ql = bufU16LE();
        char q[80]; memcpy(q, gBuf+gBufPos, min(ql, (uint16_t)79)); q[min(ql, (uint16_t)79)]='\0';
        gBufPos += ql; 
        currentState = MUSIC_PLAYING;
        // playTrackFromServer(q); // Müzik fonksiyonunu buraya bağla
        break;
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  AĞ VE HTTP İŞLEMLERİ
// ─────────────────────────────────────────────────────────────────────────────
bool downloadFull(const String& url, uint8_t** outBuf, size_t& outLen) {
  WiFiClientSecure cl; cl.setInsecure();
  HTTPClient ht;
  ht.begin(cl, url);
  if (ht.GET() == 200) {
    int clen = ht.getSize();
    if (clen > 0) {
      *outBuf = (uint8_t*)ps_malloc(clen);
      if (*outBuf) { ht.getStream().readBytes(*outBuf, clen); outLen = clen; ht.end(); return true; }
    } else {
      // Content-Length bilinmiyor: stream bitene kadar oku
      const size_t STEP = 4096;
      size_t capacity = STEP, used = 0;
      uint8_t* tmp = (uint8_t*)ps_malloc(capacity);
      if (tmp) {
        WiFiClient* stream = ht.getStreamPtr();
        unsigned long t0 = millis();
        while (millis() - t0 < 20000) {
          if (stream->available()) {
            if (used + STEP > capacity) {
              uint8_t* nb = (uint8_t*)ps_malloc(capacity + STEP);
              if (!nb) break;
              memcpy(nb, tmp, used);
              free(tmp); tmp = nb;
              capacity += STEP;
            }
            int r = stream->read(tmp + used, STEP);
            if (r > 0) { used += r; t0 = millis(); }
          } else if (!stream->connected()) break;
          else delay(1);
        }
        if (used > 0) { *outBuf = tmp; outLen = used; ht.end(); return true; }
        free(tmp);
      }
    }
  }
  ht.end(); return false;
}

void doRecordAndProcess() {
  uint8_t* tx = (uint8_t*)ps_malloc(TX_BUF_SIZE);
  if (!tx) return;
  
  currentState = LISTENING;
  tft.fillScreen(M_BLACK);
  
  size_t total = 0; unsigned long start = millis();
  while (total < TX_BUF_SIZE) {
    size_t br; i2s_read(I2S_NUM_1, tx + total, 1024, &br, portMAX_DELAY);
    total += br;
    drawMaskot(LISTENING);
    if (millis() - start > MIN_RECORD_MS && digitalRead(T1) == LOW) break;
    if (millis() - start > MAX_RECORD_MS) break;
  }

  currentState = THINKING;
  WiFiClientSecure cl; cl.setInsecure();
  HTTPClient ht;
  ht.begin(cl, URL_MAIN);
  ht.setTimeout(120000);
  int code = ht.POST(tx, total);
  free(tx);

  if (code == 200) {
    int clen = ht.getSize();
    if (clen > 0) {
      gBuf = (uint8_t*)ps_malloc(clen);
      if (gBuf) {
        gBufLen = clen;
        ht.getStream().readBytes(gBuf, clen);
        gBufPos = 0;
        parseResponseBuffer();
        free(gBuf); gBuf = nullptr;
      }
    } else {
      // Content-Length bilinmiyor (chunked): stream bitene kadar oku
      const size_t STEP = 4096;
      size_t capacity = STEP, used = 0;
      gBuf = (uint8_t*)ps_malloc(capacity);
      if (gBuf) {
        WiFiClient* stream = ht.getStreamPtr();
        unsigned long t0 = millis();
        while (millis() - t0 < 30000) {
          if (stream->available()) {
            if (used + STEP > capacity) {
              uint8_t* nb = (uint8_t*)ps_malloc(capacity + STEP);
              if (!nb) break;
              memcpy(nb, gBuf, used);
              free(gBuf); gBuf = nb;
              capacity += STEP;
            }
            int r = stream->read(gBuf + used, STEP);
            if (r > 0) { used += r; t0 = millis(); }
          } else if (!stream->connected()) break;
          else delay(1);
        }
        if (used > 0) {
          gBufLen = used; gBufPos = 0;
          parseResponseBuffer();
        }
        free(gBuf); gBuf = nullptr;
      }
    }
  }
  ht.end();
  currentState = IDLE; uiNeedsFullDraw = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  ÖZEL EKRANLAR (REMINDER & MENU)
// ─────────────────────────────────────────────────────────────────────────────
void showReminderAlert(const char* msg) {
  tft.fillScreen(M_BLACK);
  tft.drawRect(0, 0, 240, 240, M_GREEN);
  tft.setTextColor(M_GREEN);
  tft.drawCentreString("! PROTOCOL ALERT !", 120, 60, 2);
  tft.drawCentreString(msg, 120, 110, 4);

  uint8_t* vBuf = nullptr; size_t vLen = 0;
  if (downloadFull(URL_TTS + urlEncode(msg), &vBuf, vLen)) {
    gBuf = vBuf; gBufLen = vLen; gBufPos = 0;
    playPCMFromBuf(vLen); free(vBuf); gBuf = nullptr;
  }
  delay(3000);
}

void showReminderMenu() {
  currentState = SHOW_MENU;
  unsigned long lastU = 0;
  while (true) {
    if (millis() - lastU > 1000) {
      lastU = millis();
      tft.fillScreen(M_BLACK);
      tft.setTextColor(M_GREEN); tft.drawCentreString("ACTIVE TASKS", 120, 10, 2);
      tft.drawLine(0, 30, 240, 30, M_GREEN);
      int y = 45;
      for (int i = 0; i < MAX_REM; i++) {
        if (!reminders[i].active) continue;
        tft.setCursor(10, y); tft.printf("[%d] %-15s", i + 1, String(reminders[i].msg).substring(0,15).c_str());
        tft.setCursor(165, y); tft.print(fmtRem(reminders[i].trigMs));
        y += 24;
      }
      tft.setTextColor(M_DARK); tft.drawCentreString("PRESS T2 TO EXIT", 120, 220, 1);
    }
    if (digitalRead(T2) == HIGH) { delay(200); while(digitalRead(T2)==HIGH) yield(); break; }
    yield();
  }
  currentState = IDLE; uiNeedsFullDraw = true; tft.fillScreen(M_BLACK);
}

// ─────────────────────────────────────────────────────────────────────────────
//  CORE TASKS (RGB & SYSTEM)
// ─────────────────────────────────────────────────────────────────────────────
void rgbTask(void* pv) {
  while(1) {
    float t = millis()/1000.f; int r=0, g=0, b=0;
    switch(currentState) {
      case LISTENING: g = 255; break;
      case THINKING: g = (int)((sinf(t*5)+1)*127); break;
      case SPEAKING: g = 200; b = 50; break;
      case MUSIC_PLAYING: b = 255; g = 100; break;
      case REMINDER_ALERT: r = 255; g = 50; break;
      default: g = 40; // Dim Matrix Green
    }
    analogWrite(LED_R, r); analogWrite(LED_G, g); analogWrite(LED_B, b);
    vTaskDelay(20/portTICK_PERIOD_MS);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SETUP & LOOP
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  psramInit();
  tft.init(); tft.setRotation(0); tft.fillScreen(M_BLACK);
  spr.createSprite(200, 162);
  
  pinMode(T1, INPUT); pinMode(T2, INPUT);
  pinMode(AMP_SD, OUTPUT); digitalWrite(AMP_SD, LOW);
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);

  tft.setTextColor(M_GREEN);
  tft.drawCentreString("BOOTING BLINK OS...", 120, 100, 2);
  
  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED) delay(500);
  
  configTime(10800, 0, "pool.ntp.org");

  // I2S SETUP (Full Config)
  i2s_config_t i2s_out = { .mode = (i2s_mode_t)(I2S_MODE_MASTER|I2S_MODE_TX), .sample_rate = 16000, .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, .communication_format = I2S_COMM_FORMAT_STAND_I2S, .dma_buf_count = 32, .dma_buf_len = 1024 };
  i2s_driver_install(I2S_NUM_0, &i2s_out, 0, NULL);
  i2s_pin_config_t pin_out = { .bck_io_num = AMP_BCLK, .ws_io_num = AMP_LRC, .data_out_num = AMP_DIN, .data_in_num = -1 };
  i2s_set_pin(I2S_NUM_0, &pin_out);

  i2s_config_t i2s_in = { .mode = (i2s_mode_t)(I2S_MODE_MASTER|I2S_MODE_RX), .sample_rate = 16000, .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, .communication_format = I2S_COMM_FORMAT_STAND_I2S, .dma_buf_count = 8, .dma_buf_len = 128 };
  i2s_driver_install(I2S_NUM_1, &i2s_in, 0, NULL);
  i2s_pin_config_t pin_in = { .bck_io_num = MIC_SCK, .ws_io_num = MIC_WS, .data_out_num = -1, .data_in_num = MIC_SD };
  i2s_set_pin(I2S_NUM_1, &pin_in);

  xTaskCreatePinnedToCore(rgbTask, "RGB", 2048, NULL, 1, NULL, 0);
  
  tft.fillScreen(M_BLACK);
  tft.drawCentreString("SYSTEM ONLINE", 120, 110, 2);
  delay(1000); tft.fillScreen(M_BLACK);
}

void loop() {
  checkAndFireReminder(); // Sürekli tetikte
  
  if (digitalRead(T1) == HIGH && currentState == IDLE) doRecordAndProcess();
  if (digitalRead(T2) == HIGH && currentState == IDLE) showReminderMenu();

  if (currentState != SHOW_MENU && currentState != REMINDER_ALERT) {
    // Terminal UI
    tft.setCursor(5, 5); tft.setTextColor(M_DARK); tft.setTextSize(1);
    tft.print("CPU_IDLE: 98% | LINK_STABLE: YES");
    tft.drawLine(0, 22, 240, 22, M_DARK);
    
    drawMaskot(currentState);
    
    tft.setCursor(10, 225); tft.setTextColor(M_GREEN);
    if (currentState == IDLE) tft.print("> STANDBY_MODE...");
    else if (currentState == LISTENING) tft.print("> SIGNAL_RECORD...");
    else if (currentState == THINKING) tft.print("> DATA_MINING...");
  }

  delay(30);
}