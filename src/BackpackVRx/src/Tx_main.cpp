#include <Arduino.h>
#include "ESP8266_WebUpdate.h"
#include <espnow.h>
#include <EEPROM.h>
#include "msp.h"
#include "msptypes.h"

#define WIFI_PIN 0
#define LED_PIN 16

#define OPCODE_SET_CHANNEL  0x01
#define OPCODE_SET_BAND     0x02
#define OPCODE_WIFI_MODE    0x0F

mspPacket_t* cachedPacket;
bool cacheFull = false;
uint32_t lastSentCache = 0;

MSP msp;

/////////// FUNCTION DEFS ///////////

void OnDataRecv(uint8_t * mac_addr, uint8_t *data, uint8_t data_len);
void sendMSPViaEspnow(mspPacket_t *packet);
void ProcessMSPPacket(mspPacket_t *packet);
void SetSoftMACAddress();

/////////////////////////////////////

uint8_t flashLED = false;

bool startWebUpdater = false;
uint8_t channelHistory[3] = {255};

uint8_t broadcastAddress[6] = {MY_UID};

void OnDataRecv(uint8_t * mac_addr, uint8_t *data, uint8_t data_len)
{
  for(int i=0; i<data_len; i++)
  {
    Serial.write(data[i]);
  }

  channelHistory[2] = channelHistory[1];
  channelHistory[1] = channelHistory[0];
  channelHistory[0] = data[8];
    
  flashLED = true;
}

void sendMSPViaEspnow(mspPacket_t *packet)
{
  uint8_t packetSize = msp.getTotalPacketSize(packet);
  uint8_t nowDataOutput[packetSize];

  uint8_t result = msp.convertToByteArray(packet, nowDataOutput);

  if (!result)
  {
    // packet could not be converted to array, bail out
    return;
  }
  
  esp_now_send(broadcastAddress, (uint8_t *) &nowDataOutput, packetSize);
}

void ProcessMSPPacket(mspPacket_t *packet)
{
  // transparently forward MSP packets via espnow to any subscribers
  cachedPacket = packet;
  cacheFull = true;
}

void SetSoftMACAddress()
{
  WiFi.mode(WIFI_STA);

  // Soft-set the MAC address to the passphrase UID for binding
  wifi_set_macaddr(STATION_IF, broadcastAddress);
}

void setup()
{
  Serial.begin(460800);

  broadcastAddress[0] = 0; // MAC address can only be set with unicast, so first byte must be even, not odd

  SetSoftMACAddress();

  EEPROM.begin(512);
  EEPROM.get(0, startWebUpdater);
  
  if (startWebUpdater)
  {
    EEPROM.put(0, false);
    EEPROM.commit();  
    BeginWebUpdate();
  }
  else
  {
    WiFi.mode(WIFI_STA);

    if(esp_now_init() != 0)
    {
      ESP.restart();
    }

    esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
    esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);

    // esp_now_register_recv_cb(OnDataRecv); 
  }

  pinMode(WIFI_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  
  flashLED = true;
} 

void loop()
{  
  // A1, A2, A1 Select this channel order to start webupdater
  // Or press the boot button
  if ( (channelHistory[0] == 0 && channelHistory[1] == 1 && channelHistory[2] == 0) || !digitalRead(WIFI_PIN) )
  {
    EEPROM.put(0, true);
    EEPROM.commit();  
    
    for (int i = 0; i < 5; i++)
    {
      digitalWrite(LED_PIN, LOW);
      delay(50);
      digitalWrite(LED_PIN, HIGH);
      delay(50);
    }

    delay(500);
    ESP.restart();
  }
  
  if (startWebUpdater)
  {
    HandleWebUpdate();
    flashLED = true;
  }
  
  if (flashLED)
  {
    flashLED = false;
    for (int i = 0; i < 4; i++)
    {
      digitalWrite(LED_PIN, LOW);
      startWebUpdater == true ? delay(50) : delay(200);
      digitalWrite(LED_PIN, HIGH);
      startWebUpdater == true ? delay(50) : delay(200);
    }
  }

  if (cacheFull && millis() - lastSentCache > 1000)
  {
    sendMSPViaEspnow(cachedPacket);
    lastSentCache = millis();
  }

  if (Serial.available())
  {
    uint8_t c = Serial.read();

    if (msp.processReceivedByte(c))
    {
      // Finished processing a complete packet
      ProcessMSPPacket(msp.getReceivedPacket());
      msp.markPacketReceived();
    }
  }
}