#include <Arduino.h>
#include <SPI.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "esp_sleep.h"

#define PIN_SCK 12
#define PIN_MOSI 11
#define PIN_CS 10
#define PIN_DC 9
#define PIN_RST 8
#define PIN_BUSY 13
#define PIN_WAKE 4
#define PIN_VBAT 5
#define PIN_LED 6

#define SERVICE_UUID "7a1c0001-5b0e-4c52-9d0b-3a7f0e1b2c01"
#define DATA_UUID "7a1c0002-5b0e-4c52-9d0b-3a7f0e1b2c01"
#define CTRL_UUID "7a1c0003-5b0e-4c52-9d0b-3a7f0e1b2c01"
#define STATUS_UUID "7a1c0004-5b0e-4c52-9d0b-3a7f0e1b2c01"

#define ROW_BYTES 16
#define ROWS 250
#define IMG_BYTES (ROW_BYTES * ROWS)

#define ADV_WINDOW_MS 60000UL
#define CONNECTED_IDLE_MS 120000UL

#define ST_READY 0
#define ST_RECEIVING 1
#define ST_INCOMPLETE 2
#define ST_REFRESHING 3
#define ST_DONE 4

static uint8_t imgBuf[IMG_BYTES];
static volatile uint32_t received = 0;
static volatile bool pendingShow = false;
static volatile bool connected = false;
static volatile uint32_t lastActivity = 0;
static BLECharacteristic *statusChar = nullptr;

static uint8_t batteryPercent() {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);
  uint32_t mv = analogReadMilliVolts(PIN_VBAT) * 2;
  long pct = map((long)mv, 3300, 4150, 0, 100);
  return (uint8_t)constrain(pct, 0, 100);
}

static void notifyStatus(uint8_t code) {
  if (!statusChar) return;
  uint8_t v[2] = {code, batteryPercent()};
  statusChar->setValue(v, 2);
  statusChar->notify();
}

static void waitBusy(uint32_t timeoutMs) {
  uint32_t t = millis();
  while (digitalRead(PIN_BUSY) == HIGH && millis() - t < timeoutMs) delay(5);
}

static void epdCmd(uint8_t c) {
  digitalWrite(PIN_DC, LOW);
  digitalWrite(PIN_CS, LOW);
  SPI.transfer(c);
  digitalWrite(PIN_CS, HIGH);
}

static void epdData(uint8_t d) {
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  SPI.transfer(d);
  digitalWrite(PIN_CS, HIGH);
}

static void epdBuf(const uint8_t *p, size_t n) {
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  for (size_t i = 0; i < n; i++) SPI.transfer(p[i]);
  digitalWrite(PIN_CS, HIGH);
}

static void epdWindow() {
  epdCmd(0x44); epdData(0x00); epdData(0x0F);
  epdCmd(0x45); epdData(0x00); epdData(0x00); epdData(0xF9); epdData(0x00);
}

static void epdCursor() {
  epdCmd(0x4E); epdData(0x00);
  epdCmd(0x4F); epdData(0x00); epdData(0x00);
}

static void epdInit() {
  digitalWrite(PIN_RST, LOW);
  delay(10);
  digitalWrite(PIN_RST, HIGH);
  delay(10);
  waitBusy(2000);
  epdCmd(0x12);
  waitBusy(2000);
  epdCmd(0x01); epdData(0xF9); epdData(0x00); epdData(0x00);
  epdCmd(0x11); epdData(0x03);
  epdWindow();
  epdCmd(0x3C); epdData(0x05);
  epdCmd(0x18); epdData(0x80);
  epdCursor();
  waitBusy(2000);
}

static void epdShow() {
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  epdInit();
  epdCursor();
  epdCmd(0x24);
  epdBuf(imgBuf, IMG_BYTES);
  epdCursor();
  epdCmd(0x26);
  epdBuf(imgBuf, IMG_BYTES);
  epdCmd(0x22); epdData(0xF7);
  epdCmd(0x20);
  waitBusy(20000);
  epdCmd(0x10); epdData(0x01);
  SPI.endTransaction();
}

static void goToSleep() {
  digitalWrite(PIN_LED, LOW);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_WAKE, 0);
  esp_deep_sleep_start();
}

class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer *s) {
    connected = true;
    lastActivity = millis();
  }
  void onDisconnect(BLEServer *s) {
    connected = false;
    lastActivity = millis();
    BLEDevice::startAdvertising();
  }
};

class DataCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    const uint8_t *d = c->getData();
    size_t n = c->getLength();
    if (n < 3) return;
    uint32_t off = d[0] | ((uint32_t)d[1] << 8);
    size_t len = n - 2;
    if (off + len > IMG_BYTES) return;
    memcpy(imgBuf + off, d + 2, len);
    received += len;
    lastActivity = millis();
  }
};

class CtrlCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    const uint8_t *d = c->getData();
    if (c->getLength() < 1) return;
    lastActivity = millis();
    switch (d[0]) {
      case 0x01:
        received = 0;
        memset(imgBuf, 0xFF, IMG_BYTES);
        notifyStatus(ST_RECEIVING);
        break;
      case 0x02:
        if (received >= IMG_BYTES) {
          pendingShow = true;
        } else {
          notifyStatus(ST_INCOMPLETE);
        }
        break;
      case 0x03:
        memset(imgBuf, 0xFF, IMG_BYTES);
        received = IMG_BYTES;
        pendingShow = true;
        break;
    }
  }
};

void setup() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, HIGH);
  pinMode(PIN_BUSY, INPUT);
  pinMode(PIN_WAKE, INPUT_PULLUP);
  SPI.begin(PIN_SCK, -1, PIN_MOSI, PIN_CS);
  memset(imgBuf, 0xFF, IMG_BYTES);

  BLEDevice::init("EInkBadge");
  BLEDevice::setMTU(247);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCb());
  BLEService *svc = server->createService(SERVICE_UUID);

  BLECharacteristic *dataChar = svc->createCharacteristic(DATA_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  dataChar->setCallbacks(new DataCb());

  BLECharacteristic *ctrlChar = svc->createCharacteristic(CTRL_UUID, BLECharacteristic::PROPERTY_WRITE);
  ctrlChar->setCallbacks(new CtrlCb());

  statusChar = svc->createCharacteristic(STATUS_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  statusChar->addDescriptor(new BLE2902());
  uint8_t initial[2] = {ST_READY, batteryPercent()};
  statusChar->setValue(initial, 2);

  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  lastActivity = millis();
}

void loop() {
  if (pendingShow) {
    pendingShow = false;
    notifyStatus(ST_REFRESHING);
    delay(100);
    epdShow();
    notifyStatus(ST_DONE);
    delay(800);
    goToSleep();
  }
  uint32_t idle = millis() - lastActivity;
  if (!connected && idle > ADV_WINDOW_MS) goToSleep();
  if (connected && idle > CONNECTED_IDLE_MS) goToSleep();
  delay(20);
}

// devvy dev devv yp
