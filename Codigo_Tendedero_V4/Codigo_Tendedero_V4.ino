#include <WiFi.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>
#include <WebSocketsServer.h>
#include <WebServer.h>
#include <Stepper.h>
#include <DHT.h>

// Página web almacenada en PROGMEM
const char webpage[] PROGMEM = R"=====(
// (Tu HTML aquí)
)=====";

// Pines de los sensores y motores
const int trigPin = 13;
const int echoPin = 14;
const int dhtPin = 15;
const int rainSensorPin = 34;

// Configuración de motores usando Stepper
const int stepsPerRevolution = 2048;  // Número de pasos por revolución (ajusta según tus motores)

// Matriz de pines para motores (2 motores con 4 pines cada uno)
int motorPins[2][4] = {
    {19, 21, 22, 23},  // Motor 1
    {2, 4, 5, 18}      // Motor 2
};

// Crear instancias de los motores usando la matriz de pines
Stepper motor1(stepsPerRevolution, motorPins[0][0], motorPins[0][1], motorPins[0][2], motorPins[0][3]);
Stepper motor2(stepsPerRevolution, motorPins[1][0], motorPins[1][1], motorPins[1][2], motorPins[1][3]);

DHT dht(dhtPin, DHT11);

// Credenciales de WiFi
const char* SSID = "INFINITUM5FAF";
const char* PASSWORD = "V6PD6qgRvk4yn";
const char* serverUrl = "http://192.168.1.78:3000/api/sensores/insertar";

// Variables para enviar datos a intervalos
unsigned long lastTimeTempHum = 0;
unsigned long lastTimeRain = 0;
unsigned long lastTimeUltrasonic = 0;

const long intervalTempHum = 10000;
const long intervalRain = 10000;
const long intervalUltrasonic = 10000;

// Servidores WebSocket y HTTP
WebSocketsServer webSocket(81);
WebServer server(80);
bool modoManual = false;

// Declaración de funciones
float readUltrasonic();
void sendDataAPI(String tipo, String nombre, String valor, String unidad);
void connectWiFi();
void sendDataToClient();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);

void setup() {
    Serial.begin(115200);
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);
    pinMode(rainSensorPin, INPUT);

    // Configuración de velocidad de los motores
    motor1.setSpeed(60); // Ajusta la velocidad del motor 1 (en RPM)
    motor2.setSpeed(60); // Ajusta la velocidad del motor 2 (en RPM)
    
    dht.begin();
    connectWiFi();

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    server.on("/", []() {
      server.send_P(200, "text/html", webpage);
    });

    // ✅ Agregamos la ruta WebSocket para evitar error 200 OK
    server.on("/ws", HTTP_GET, []() { 
        server.send(101); 
    });

    server.begin();
    Serial.println("✅ Servidor HTTP iniciado");
}

void loop() {
  webSocket.loop();
  server.handleClient();
  sendDataToClient();
  scanParams();

  // Llamar a step() para mover los motores de forma continua
  motor1.step(0); // Mueve el motor 1 según los pasos definidos
  motor2.step(0); // Mueve el motor 2 según los pasos definidos
}

// Función para conectar a WiFi
void connectWiFi() {
    Serial.print("Conectando a WiFi...");
    WiFi.begin(SSID, PASSWORD);
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
        delay(1000);
        Serial.print("...");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ Conexión WiFi exitosa");
        Serial.print("📡 Dirección IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n❌ No se pudo conectar al WiFi. Reiniciando...");
        ESP.restart();
    }
}

// Función para escanear sensores y enviar datos
void scanParams() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastTimeTempHum > intervalTempHum) {
    lastTimeTempHum = currentMillis;
    float temperatura = dht.readTemperature();
    if (!isnan(temperatura)) {
      sendDataAPI("Sensor", "Temperatura", String(temperatura), "°C");
    }

    float humedad = dht.readHumidity();
    if (!isnan(humedad)) {
      sendDataAPI("Sensor", "Humedad", String(humedad), "%");
    }
  }

  if (currentMillis - lastTimeRain > intervalRain) {
    lastTimeRain = currentMillis;
    int lluvia = digitalRead(rainSensorPin);
    sendDataAPI("Sensor", "Lluvia", String(lluvia), "digital");
  }

  if (currentMillis - lastTimeUltrasonic > intervalUltrasonic) {
    lastTimeUltrasonic = currentMillis;
    float distance = readUltrasonic();
    if (distance >= 0) {
      sendDataAPI("Sensor", "Ultrasonico", String(distance), "cm");
    }
  }
}

// Función para leer sensor ultrasónico
float readUltrasonic() {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    long duration = pulseIn(echoPin, HIGH);
    return (duration / 2.0) / 29.1;
}

// Enviar datos a la API
void sendDataAPI(String tipo, String nombre, String valor, String unidad) {
 if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"tipo\":\""+tipo+"\",\"nombre\":\""+nombre+"\",\"valor\":\""+valor+"\",\"unidad\":\""+unidad+"\"}";
    int httpResponseCode = http.POST(payload);

    Serial.println("Enviando: "+payload);
    Serial.println("Respuesta HTTP: "+String(httpResponseCode));
    
    http.end();
  } else {
    Serial.println("Error en conexión WiFi. No se enviaron datos.");
  }
}

// Enviar datos al cliente WebSocket
void sendDataToClient() {
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  long duration, distance;
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH);
  distance = (duration / 2) / 29.1;

  String distanceMessage = (distance >= 0 && distance < 10) ? "Se detecta un objeto" : "No hay nada";

  String json = "{\"temperatura\":" + String(temp) +
              ",\"humedad\":" + String(hum) +
              ",\"distance\":\"" + distanceMessage + "\"" +
              ",\"modoManual\":" + (modoManual ? "true" : "false") + "}";

  webSocket.broadcastTXT(json);
}

// Manejo de mensajes WebSocket
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    if (type == WStype_TEXT) {
        String message = String((char *)payload);
        Serial.println("Mensaje recibido: " + message);

        if (message == "Retraer") {
            motor1.step(-500);  // Mover el motor 1 hacia atrás
            motor2.step(-500);  // Mover el motor 2 hacia atrás
        } 
        else if (message == "Extender") {
            motor1.step(500);  // Mover el motor 1 hacia adelante
            motor2.step(500);  // Mover el motor 2 hacia adelante
        }
        else if (message == "ToggleManual") {
            modoManual = !modoManual;
            String json = "{\"modoManual\":" + String(modoManual ? "true" : "false") + "}";
            webSocket.broadcastTXT(json);
        }
    }
}
