#include "HX711.h"
#include "Tone32.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HardwareSerial.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

#define DOUT  17
#define CLK  16
#define BUZZER_PIN 19
#define BUZZER_CHANNEL 0

#define RXBARCODE 23


#define MQTT_SERVER "134.209.237.109"

WiFiClient espClient;
PubSubClient client(espClient);

HX711 scale;
LiquidCrystal_I2C lcd(0x27, 18, 2);

WiFiManager wm;
TaskHandle_t WifiTask;

float calibration_factor = 455; //change according to default board mass
float reading_t0 = 0;
float reading_t1 = 0;
bool object_placed = false;
bool object_scanned = false;
bool wifi_status_notified = false;

char eancode[14];
char msg[60];
byte i = 0;


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

void setup_wifi() {
//    const char *ssid = WIFI1;
//    const char *password = WIFI1_PWD;
//    if (WiFi.status() == WL_CONNECTED) return;
//    lcd.clear();
//    lcd.setCursor(0, 0);
//    lcd.print("Connecting WIFI");
//    lcd.setCursor(0, 1);
//    lcd.print(WIFI1);
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
}

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
        if (client.connect("Client_ESP32", "mq_device", "mq2")) {
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

void setup() {

    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Kalibracija");

    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, RXBARCODE, 10); // TX pin not used

    WiFi.mode(WIFI_STA);
    wm.setDebugOutput(false);
//    wm.resetSettings();
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
    Serial.print(
            "Zero factor: "); //This can be used to remove the need to tare the scale. Useful in permanent scale projects.
    Serial.println(zero_factor);
    scale.set_scale(calibration_factor); //Adjust to this calibration factor

    lcd.setCursor(0, 0);
    lcd.print("Skenejiet pudeli!");
    lcd.setCursor(0, 1);
    if (WiFi.status() != WL_CONNECTED) {
        lcd.print("Wifi: NEAKTIVS");
    } else {
        lcd.print("W:" + WiFi.SSID());
    }
}

void loop() {

    reconnect();

    if (Serial2.available()) {
        i = 0;
        while (Serial2.available()) {
            char x = Serial2.read();
            eancode[i++] = x;
        }
        eancode[13] = 0; //null terminated string
        Serial.print("Barcode received: ");
        Serial.println(eancode);
        lcd.clear();
        lcd.setCursor(0, 0);
//        lcd.print("EAN: ");

        lcd.print(eancode);
        lcd.setCursor(0, 1);
        lcd.print("Svars ...");
        object_scanned = true;
    }

    reading_t0 = scale.get_units(2);
    if (reading_t0 > 10.0) {
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
        tone(BUZZER_PIN, NOTE_C4, 200, BUZZER_CHANNEL);
        Serial.print("Reading: ");
        float avg = (reading_t1 + reading_t0) / 2.0;
        Serial.printf("%.2f g", avg);
        Serial.println();
        reading_t0 = 0;
        reading_t1 = 0;
//        lcd.clear();
//        lcd.setCursor(0,0);
//        lcd.print("Gatavs!");
        lcd.setCursor(0, 1);
        lcd.print("Svars: ");
        lcd.printf("%.0f g", avg);

        sprintf(msg, "{\"id\":\"pNvkH9sk8s5BdnMQQe\",\"b\":\"%s\",\"w\":\"%.0f\"}", eancode, avg);
        client.publish("INVENTORY", msg);

        object_scanned = false;
    }


//    Serial.print("Reading: ");
//    Serial.print(scale.get_units(), 1);
//    Serial.print(" grams");
//    Serial.print(" calibration_factor: ");
//    Serial.print(calibration_factor);
//    Serial.println();

//    if (Serial.available()) {
//        int val = Serial.parseInt(); //read int or parseFloat for ..float...
//        if (val > 0) {
//            calibration_factor = val;
//            // scale.set_scale(calibration_factor); //Adjust to this calibration factor
//        }
//    }


}
