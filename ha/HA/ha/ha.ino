// ============================================================
// ESP32-S3 + DHT22 → MQTT (Home Assistant)
// v3.2 — Configurado para DHT22 e Monitoramento de Status
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHTesp.h>
//#include "senhas.h"

// ------- CONFIGURAÇÕES — edite aqui -------
const char* WIFI_SSID     = "BRUGER_2G";
const char* WIFI_PASSWORD = "Gersones68";

const char* MQTT_BROKER   = "homeassistant.local";
const int   MQTT_PORT     = 1883;
const char* MQTT_USER     = "mqtt";
const char* MQTT_PASSWORD = "Gabibi89*";

// Tópicos de dados
const char* TOPIC_TEMP    = "casa/sala/temperatura";
const char* TOPIC_HUMID   = "casa/sala/umidade";
const char* TOPIC_STATUS  = "casa/sala/status"; 
const char* CLIENT_ID     = "esp32s3-sala";

const int   DHT_PIN       = 4; // Pino de dados conectado no GPIO4 do ESP32-S3
const long  INTERVAL_MS   = 10000;   // intervalo de publicação
const long  RECONECT_MS   = 5000;    // intervalo entre tentativas de reconexão
 
// Tópicos de Discovery para o Home Assistant
const char* DISCOVERY_TEMP   = "homeassistant/sensor/esp32s3_sala/temperatura/config";
const char* DISCOVERY_HUMID  = "homeassistant/sensor/esp32s3_sala/umidade/config";
const char* DISCOVERY_STATUS = "homeassistant/binary_sensor/esp32s3_sala/status/config";
// ------------------------------------------

DHTesp       dht;
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

unsigned long lastPublish  = 0;
unsigned long lastReconect = 0;

// ------- Verifica e reconecta o WiFi (sem bloquear o loop) -------
void verificarWifi() {
  if (WiFi.status() == WL_CONNECTED) return; 

  unsigned long agora = millis();
  if (agora - lastReconect < RECONECT_MS) return; 
  lastReconect = agora;

  Serial.println("WiFi desconectado! Tentando reconectar...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 8000) {
    delay(200);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi reconectado! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nFalhou. Tentará novamente em breve.");
  }
}

// ------- Verifica e reconecta o MQTT (sem bloquear o loop) -------
void verificarMQTT() {
  if (!mqtt.connected()) {
    unsigned long agora = millis();
    if (agora - lastReconect < RECONECT_MS) return;
    lastReconect = agora;

    Serial.print("Reconectando ao MQTT...");
    
    if (mqtt.connect(CLIENT_ID, MQTT_USER, MQTT_PASSWORD, TOPIC_STATUS, 1, true, "offline")) {
      Serial.println(" ok!");
      publicarDiscovery(); 
      mqtt.publish(TOPIC_STATUS, "online", true); 
    } else {
      Serial.print(" falhou. rc=");
      Serial.println(mqtt.state());
    }
  }
}

// Registra os tópicos no Home Assistant via MQTT Discovery
void publicarDiscovery() {
  mqtt.setBufferSize(512); 

  // --- Temperatura ---
  String payloadTemp = R"({
    "name": "Temperatura Sala",
    "state_topic": "casa/sala/temperatura",
    "unit_of_measurement": "°C",
    "device_class": "temperature",
    "unique_id": "esp32s3_sala_temp",
    "suggested_area": "Sala"
  })"; 

  mqtt.publish(DISCOVERY_TEMP, payloadTemp.c_str(), true);

  // --- Umidade ---
  String payloadHumid = R"({
    "name": "Umidade Sala",
    "state_topic": "casa/sala/umidade",
    "unit_of_measurement": "%",
    "device_class": "humidity",
    "unique_id": "esp32s3_sala_humid",
    "suggested_area": "Sala"
  })"; 

  mqtt.publish(DISCOVERY_HUMID, payloadHumid.c_str(), true);

  // --- Status de Conectividade (Binary Sensor) ---
  String payloadStatus = R"({
    "name": "ESP32-S3 Conexão",
    "state_topic": "casa/sala/status",
    "payload_on": "online",
    "payload_off": "offline",
    "device_class": "connectivity",
    "unique_id": "esp32s3_sala_status",
    "suggested_area": "Sala"
  })"; 

  mqtt.publish(DISCOVERY_STATUS, payloadStatus.c_str(), true);

  Serial.println("MQTT Discovery do ESP32-S3 publicado!");
}

// ------- Setup -------
void setup() {
  Serial.begin(115200);
  
  // CONFIGURAÇÃO DO SENSOR ATUALIZADA PARA DHT22
  dht.setup(DHT_PIN, DHTesp::DHT22); 

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado! IP: " + WiFi.localIP().toString());

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);

  if (mqtt.connect(CLIENT_ID, MQTT_USER, MQTT_PASSWORD, TOPIC_STATUS, 1, true, "offline")) {
    publicarDiscovery(); 
    mqtt.publish(TOPIC_STATUS, "online", true);
  }
}

// ------- Loop -------
void loop() {
  verificarWifi();  

  if (WiFi.status() == WL_CONNECTED) {
    verificarMQTT();  
    mqtt.loop();
  }

  unsigned long agora = millis();
  if (agora - lastPublish >= INTERVAL_MS) {
    lastPublish = agora;

    if (WiFi.status() == WL_CONNECTED && mqtt.connected()) {
      TempAndHumidity dados = dht.getTempAndHumidity();

      if (dht.getStatus() == DHTesp::ERROR_NONE) {
        char bufTemp[8];
        char bufHumid[8];
        
        // O DHT22 trabalha bem com floats de precisão, mantendo uma casa decimal (.1)
        dtostrf(dados.temperature, 4, 1, bufTemp);
        dtostrf(dados.humidity,    4, 1, bufHumid);

        mqtt.publish(TOPIC_TEMP,  bufTemp);
        mqtt.publish(TOPIC_HUMID, bufHumid);

        Serial.printf("Publicado → Temp: %s°C | Umidade: %s%%\n", bufTemp, bufHumid);
      } else {
        // Exibe o tipo de erro específico caso falhe (ex: TIMEOUT)
        Serial.printf("Erro na leitura do DHT22: %s — pulando publicação\n", dht.getStatusString());
      }
    } else {
      Serial.println("Sem conexão — publicação adiada.");
    }
  }
}