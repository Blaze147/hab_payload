
#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

// BSFrance LoRa32u4 II radio pins
#define LORA_CS   8
#define LORA_RST  4
#define LORA_DIO0 7

#define LORA_FREQ 915E6

void setup() {
    Serial.begin(115200);

    LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

    if (!LoRa.begin(LORA_FREQ)) {
        Serial.println("LoRa initialization failed!");
        while (true) {
            delay(1000);
        }
    }

    Serial.println("LoRa initialized!");

#ifdef RADIO_TRANSMITTER
    Serial.println("Operating as TRANSMITTER");
#endif

#ifdef RADIO_RECEIVER
    Serial.println("Operating as RECEIVER");
#endif
}

void loop() {

#ifdef RADIO_TRANSMITTER
    static unsigned long counter = 0;

    LoRa.beginPacket();
    LoRa.print("Hello from transmitter: ");
    LoRa.print(counter++);
    LoRa.endPacket();

    Serial.println("Packet sent!");
    delay(2000);
#endif

#ifdef RADIO_RECEIVER
    int packetSize = LoRa.parsePacket();

    if (packetSize > 0) {
        Serial.print("Received: ");

        while (LoRa.available()) {
            Serial.print((char)LoRa.read());
        }

        Serial.print(" | RSSI: ");
        Serial.println(LoRa.packetRssi());
    }
#endif

}
