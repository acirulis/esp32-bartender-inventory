#include "HX711.h"
#include <Wire.h>
#include "Tone32.h"
#include <LiquidCrystal_I2C.h>
#include <HardwareSerial.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

// G..13.11.9..8.17.15.6....3v3
 
// LCD:
// 	VCC -> 5V
// 	GND -> GND
// 	SDA -> 8
// 	SCL -> 9
// HX711:
// 	VCC -> 3V3
// 	GND -> GND
// 	DT  -> 17
// 	SCK -> 15
// LV2097:
// 	2   -> 3V3
// 	3   -> GND 
// 	4   -> 11
// 	5   -> 13
// 	10  -> LED VCC
// Buzzer:
// 	GND -> GND
// 	VCC -> 6
// LED:
// 	GND -> GND
// 	VCC -> LV2097 10

#define DOUT  17 //load cell to HX711
#define CLK  15 //load acell to HX711
#define BUZZER_PIN 6
#define BUZZER_CHANNEL 0

#define RXBARCODE 13 // TTL Serial interface SERIAL_8N1

#define MQTT_SERVER "207.154.238.45"

WiFiClient espClient;
PubSubClient client(espClient);

HX711 scale;
LiquidCrystal_I2C lcd(0x27, 16, 2);

WiFiManager wm;
TaskHandle_t WifiTask;

float calibration_factor = 472.22; //change according to default board mass
float calibration_weight = 500;
float reading_t0 = 0;
float reading_t1 = 0;
bool object_placed = false;
bool object_scanned = false;
bool wifi_status_notified = false;
float change;

char eancode[14];
char lastread[14];
char msg[60];
byte i = 0;
long currmillis;

void Scanner() {
    Serial.println();
    Serial.println("I2C scanner. Scanning ...");
    byte count = 0;

    Wire.begin();
    for (byte x = 8; x < 120; x++) {
        Wire.beginTransmission(x);          // Begin I2C transmission Address (i)
        if (Wire.endTransmission() == 0)  // Receive 0 = success (ACK response)
        {
            Serial.print("Found address: ");
            Serial.print(x, DEC);
            Serial.print(" (0x");
            Serial.print(x, HEX);     // PCF8574 7 bit address
            Serial.println(")");
            count++;
        }
    }
    Serial.print("Found ");
    Serial.print(count, DEC);        // numbers of devices
    Serial.println(" device(s).");
}


[[noreturn]] void WifiTaskRunner(void *pvParameters) {
//    if (wm.autoConnect("BARTENDER-connect-me", "12345678")) {
    if (wm.autoConnect("BARTENDER-configure-me")) {
        Serial.println("connected...yeey :)");
    } else {
        Serial.println("Configportal running");
    }
    while (true) {
        wm.process();
        vTaskDelay(50);
    }
}

// void setup_wifi() {
//    const char *ssid = "WD_guests";
//    const char *password = "Spikeri21";
//    if (WiFi.status() == WL_CONNECTED) return;
//    lcd.clear();
//    lcd.setCursor(0, 0);
//    lcd.print("Connecting WIFI");
//    lcd.setCursor(0, 1);
//    lcd.print(ssid);
//    Serial.print("Starting Wifi connection to ");
//    Serial.println(ssid);
//    WiFi.mode(WIFI_STA);
//    WiFi.begin(ssid, password);
//    int failed_counter = 0;
//    while (WiFi.status() != WL_CONNECTED) {
//        delay(1000);
//        Serial.print(".");
//        failed_counter++;
//        if (failed_counter > 5) {
//            Serial.println();
//            Serial.println("Wifi connection failed");
//            return;
//        }
//    }
//    Serial.println();
//    Serial.println("WiFi connected");
//    Serial.print("IP address: ");
//    Serial.println(WiFi.localIP());
// }

void reconnect() {
//    setup_wifi();
    if (WiFi.status() != WL_CONNECTED) return; // continue to MQTT only if WIFI ready
    const char *mqtt_server = MQTT_SERVER;
    client.setServer(mqtt_server, 1883);
    client.setKeepAlive(120);
    int failed_counter = 0;
    while (!client.connected()) {
        Serial.print("MQTT connect...");
        Serial.print("Attempting MQTT connection...");
        if (client.connect("Client_ESP32", "mq_device", "eU42MzLqXLXV")) {
            Serial.println("connected");
            wifi_status_notified = false;
        } else {
            Serial.print("failed, rc=");
            Serial.println(client.state());
            Serial.println("Try again in 500ms");
            delay(500);
            failed_counter++;
            if (failed_counter > 4) {
                Serial.println("MQTT connection failed");
                lcd.setCursor(0, 1);
                lcd.print("MQTT: Error");
                return;
            }
        }
    }
}

void Calibrate()
{
    change = 256;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Calibrating...");
    lcd.setCursor(0, 1);
    lcd.print("Do not touch wgt");
    while(change > 0.001)
    {
        float now = scale.get_units(2);
        Serial.printf("%.2f g", now);
        Serial.printf(" ar kalibraciju ");
        Serial.printf("%.2f", calibration_factor);
        change/=2;
        if(now > calibration_weight)
        {
            calibration_factor+=change;
            Serial.printf(" (+");
        }
        else
        {
            calibration_factor-=change;
            Serial.printf(" (-");
        }
        Serial.printf("%2.f)\n", change);
        scale.set_scale(calibration_factor);
    }
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Calib faktors: ");
    lcd.setCursor(0, 1);
    lcd.print(calibration_factor);
    delay(2000);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Svars tagad: ");
    lcd.setCursor(0, 1);
    lcd.print(scale.get_units(2));
    delay(2000);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Skenejiet pudeli!");
    lcd.setCursor(0, 1);
    if (WiFi.status() != WL_CONNECTED)
    {
        lcd.print("Wifi: NEAKTIVS");
    }
    else
    {
        lcd.print("W:" + WiFi.SSID());
    }
}

void SetupBarcode()
{
    bool doSetup = false;
    byte modequery[] = {0x7E, 0x01, 0x30, 0x30, 0x30, 0x30, 0x40, 0x53, 0x43, 0x4e, 0x4d, 0x4f, 0x44, 0x2a, 0x3B, 0x03};
    
    Serial2.write(modequery, sizeof(modequery));
    
    delay(100);
    if (Serial2.available() and not object_placed and not object_scanned)
    {
        i = 0;
        while (Serial2.available())
        {
            char y = Serial2.read();
            Serial.printf("%c", y);
            if(i == 13 && y != 33)
            {
                doSetup = true;
            }
            i++;
        }
    }
    Serial.println(doSetup);
    if(doSetup)
    {
        byte scanMode3[] = {0x7E, 0x01, 0x30, 0x30, 0x30, 0x30, 0x40, 0x53, 0x43, 0x4e, 0x4d, 0x4f, 0x44, 0x33, 0x3B, 0x03};
        byte reread[] = {0x7E, 0x01, 0x30, 0x30, 0x30, 0x30, 0x40, 0x52, 0x52, 0x44, 0x45, 0x4e, 0x41, 0x31, 0x3b, 0x03};
        byte rereadLength[] = {0x7E, 0x01, 0x30, 0x30, 0x30, 0x30, 0x40, 0x52, 0x52, 0x44, 0x44, 0x55, 0x52, 0x32, 0x30, 0x30, 0x30, 0x3b, 0x03};
        byte interf[] = {0x7E, 0x01, 0x30, 0x30, 0x30, 0x30, 0x40, 0x49, 0x4e, 0x54, 0x45, 0x52, 0x46, 0x30, 0x3b, 0x03};
        //byte baudRate[] = {0x7E, 0x01, 0x30, 0x30, 0x30, 0x30, 0x40, 0x32, 0x33, 0x32, 0x42, 0x41, 0x44, 0x38, 0x3b, 0x03};
        Serial2.write(scanMode3, sizeof(scanMode3));
        delay(100);
        Serial2.write(reread, sizeof(reread));
        delay(100);
        Serial2.write(rereadLength, sizeof(rereadLength));
        delay(100);
        Serial2.write(interf, sizeof(interf));
        delay(100);
        //Serial2.write(baudRate, sizeof(baudRate));
        while (Serial2.available())
        {
            char y = Serial2.read();
        }
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Works");
    Scanner();
    lcd.init();
    lcd.backlight();
    // lcd.noBacklight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Kalibracija");

    Serial.println("Kalibracij");
    Serial2.begin(9600, SERIAL_8N1, RXBARCODE, 11); // TX pin not used
    SetupBarcode();
    WiFi.mode(WIFI_STA);
    wm.setDebugOutput(false);
    // wm.resetSettings();
    wm.setConfigPortalBlocking(false);

    xTaskCreatePinnedToCore(
            WifiTaskRunner,   /* Task function. */
            "WifiTask",     /* name of task. */
            10000,       /* Stack size of task */
            NULL,        /* parameter of the task */
            1,           /* priority of the task */
            &WifiTask,      /* Task handle to keep track of created task */
            0);          /* pin task to core 0 */
    scale.begin(DOUT, CLK);
    scale.set_scale();
    scale.tare(); //Reset the scale to 0
    long zero_factor = scale.read_average(); //Get a baseline reading
    Serial.print("Zero factor: "); //This can be used to remove the need to tare the scale. Useful in permanent scale projects.
    Serial.println(zero_factor);
    scale.set_scale(calibration_factor); //Adjust to this calibration factor
    lcd.setCursor(0, 0);
    lcd.print("Skenejiet pudeli!");
    lcd.setCursor(0, 1);
    if (WiFi.status() != WL_CONNECTED)
    {
        lcd.print("Wifi: NEAKTIVS");
    }
    else
    {
        lcd.print("W:" + WiFi.SSID());
    }
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
    tone(BUZZER_PIN, NOTE_C6, 100, BUZZER_CHANNEL);
}

void loop() {

    reconnect();
    if (Serial2.available() and not object_placed and not object_scanned)
    {
        i = 0;
        while (Serial2.available())
        {
            char x = Serial2.read();
            eancode[i++] = x;
        }
        eancode[i] = 0;
        if(eancode[0] == 0)
        {
            Serial.println("Nullcode");
        }
        else if(eancode[0] == 2)
        {
            Serial.println("Command return");
            Serial.println(eancode);
            if(eancode[13] == '0')
            {
                byte scanMode3[] = {0x7E, 0x01, 0x30, 0x30, 0x30, 0x30, 0x40, 0x53, 0x43, 0x4e, 0x4d, 0x4f, 0x44, 0x33, 0x3B, 0x03};
                Serial2.write(scanMode3, sizeof(scanMode3));
            }
            object_scanned = false;
        }
        else if(i > 14)
        {
            Serial.println("Barcode too long");
            lcd.clear();
             lcd.setCursor(0, 0);
             lcd.print("Too fast");
             delay(1000);
             object_scanned = false;
        }
        else
        {
            eancode[i] = 0;
            Serial.print("Barcode received: ");
            Serial.println(eancode);
            byte scanMode0[] = {0x7E, 0x01, 0x30, 0x30, 0x30, 0x30, 0x40, 0x53, 0x43, 0x4e, 0x4d, 0x4f, 0x44, 0x30, 0x3B, 0x03};
            Serial2.write(scanMode0, sizeof(scanMode0));
            if(!strcmp(eancode, "CALIBRATION..."))
            {
                Calibrate();
            }
            else
            {
                tone(BUZZER_PIN, NOTE_G5, 100, BUZZER_CHANNEL);
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("EAN:  Svars: ...");
                lcd.setCursor(0, 1);
                lcd.print(eancode);
                object_scanned = true;
            }
        }
    }
     reading_t0 = scale.get_units(2);
     if (reading_t0 > 10.0) {
         delay(500);
         reading_t1 = scale.get_units(2);
     } else {
         if (object_placed || !wifi_status_notified) {
             lcd.clear();
             lcd.setCursor(0, 0);
             lcd.print("Skenejiet pudeli!");
             lcd.setCursor(0, 1);
             if (WiFi.status() != WL_CONNECTED) {
                 lcd.print("Wifi: NEAKTIVS");
            } else {
                lcd.print("W:" + WiFi.SSID());
            }
        }
        reading_t1 = 0;
        object_placed = false;
        wifi_status_notified = true;
    }
    if ((reading_t0 > 10.0) and (reading_t1 > 10.0) and (abs(reading_t0 - reading_t1) < 5.0) and not object_placed and
        object_scanned) {
        object_placed = true;
        tone(BUZZER_PIN, NOTE_C5, 100, BUZZER_CHANNEL);
        Serial.print("Reading: ");
        float avg = (reading_t1 + reading_t0) / 2.0;
        Serial.printf("%.2f g", avg);
        Serial.println();
        reading_t0 = 0;
        reading_t1 = 0;
       lcd.clear();
       lcd.setCursor(0,0);
       lcd.print("Gatavs!");
        lcd.setCursor(0, 1);
        lcd.print("Svars: ");
        lcd.printf("%.0f g", avg);

        sprintf(msg, R"({"b":"%s","w":"%.0f"})", eancode, avg);
        client.publish("barspector/id001", msg);

        object_scanned = false;
    }
}
