#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebSrv.h>
#include <math.h>

// Wi-Fi credentials
const char* ssid = "iPhone";
const char* password = "mattmia7";

// Sensor pins
const int xPin = 34;
const int yPin = 35;
const int zPin = 32;
const int emgPin = 36;
const int contactMicPin = 33;
const int thermistorPin = 39;

// Thresholds
const int contactThreshold = 2000;
const float seriesResistor = 10000.0;
const float wearThreshold = 8500.0;

// Globals
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

unsigned long lastSampleTime = 0;
int emgValue = 0;
int contactMicValue = 0;
bool isWorn = false;
int eventCounter = 0;
bool wasAboveThreshold = false;

// Get orientation from accelerometer
String getOrientation() {
    int rawX = analogRead(xPin);
    int rawY = analogRead(yPin);
    int rawZ = analogRead(zPin);

    float voltageX = (rawX / 4095.0) * 3.3;
    float voltageY = (rawY / 4095.0) * 3.3;
    float voltageZ = (rawZ / 4095.0) * 3.3;

    float accelX = (voltageX - 1.5) / 0.06;
    float accelY = (voltageY - 1.5) / 0.06;
    float accelZ = (voltageZ - 1.5) / 0.06;

    if (accelY > 0.0) return "Laying Down on Left Side";
    if (accelY < -1.2) return "Laying Down on Right Side";
    if (accelZ > 0.2) return "Laying Down on Back";
    if (accelX > 0.0 && accelY > -1.0 && accelZ < 0.0) return "Standing Up";
    return "Unknown Position";
}

// Check if device is worn via thermistor
bool checkWearStatus() {
    int adcValue = analogRead(thermistorPin);
    float voltage = adcValue * (3.3 / 4095.0);
    float resistance = (seriesResistor * (3.3 - voltage)) / voltage;
    return resistance < wearThreshold;
}

// Sensor Sampling Task
void sampleSensors(void *parameter) {
    char message[128];

    while (true) {
        unsigned long currentTime = micros();
        if (currentTime - lastSampleTime >= 100000) { // 100 ms = 10Hz
            lastSampleTime += 100000;

            emgValue = analogRead(emgPin);
            contactMicValue = analogRead(contactMicPin);
            isWorn = checkWearStatus();
            String orientation = getOrientation();

            // Threshold logic (contact mic only)
            if (contactMicValue > contactThreshold && !wasAboveThreshold) {
                eventCounter++;
                wasAboveThreshold = true;
                Serial.println("📈 Contact Mic Event! Counter: " + String(eventCounter));
            } else if (contactMicValue <= contactThreshold) {
                wasAboveThreshold = false;
            }

            snprintf(message, sizeof(message),
                "{ \"emg\": %d, \"contactMic\": %d, \"orientation\": \"%s\", \"isWorn\": %s, \"counter\": %d }",
                emgValue, contactMicValue, orientation.c_str(), isWorn ? "true" : "false", eventCounter);

            if (ws.count() > 0) {
                ws.textAll(message);
            }

            vTaskDelay(1);
        }
    }
}

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n✅ Connected to WiFi!");
    Serial.println("ESP32 IP Address: " + WiFi.localIP().toString());

    // WebSocket
    ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                  void *arg, uint8_t *data, size_t len) {
        if (type == WS_EVT_DISCONNECT) {
            Serial.println("⚠️ WebSocket Disconnected.");
            ws.cleanupClients();
        }
    });
    server.addHandler(&ws);

    // HTML UI
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = "<html><head><style>";
        html += "body { font-family: Arial; text-align: center; }";
        html += "h1 { color: #333; } p { font-size: 24px; }</style></head><body>";
        html += "<h1>ESP32 Sensor Web Server</h1>";
        html += "<p>Orientation: <b><span id='orientation'>Waiting...</span></b></p>";
        html += "<p>EMG Value: <b><span id='emg'>Waiting...</span></b></p>";
        html += "<p>Contact Mic Value: <b><span id='contactMic'>Waiting...</span></b></p>";
        html += "<p>Device Worn: <b><span id='isWorn'>Waiting...</span></b></p>";
        html += "<p>Event Counter: <b><span id='counter'>0</span></b></p>";
        html += "<script>";
        html += "let ws = new WebSocket('ws://' + window.location.host + '/ws');";
        html += "ws.onmessage = function(event) {";
        html += "try {";
        html += "let data = JSON.parse(event.data);";
        html += "if (data.emg !== undefined) document.getElementById('emg').innerText = data.emg;";
        html += "if (data.contactMic !== undefined) document.getElementById('contactMic').innerText = data.contactMic;";
        html += "if (data.orientation !== undefined) document.getElementById('orientation').innerText = data.orientation;";
        html += "if (data.isWorn !== undefined) document.getElementById('isWorn').innerText = data.isWorn ? 'Yes' : 'No';";
        html += "if (data.counter !== undefined) document.getElementById('counter').innerText = data.counter;";
        html += "} catch (error) { console.error('WebSocket JSON error:', error); }";
        html += "};";
        html += "</script></body></html>";
        request->send(200, "text/html", html);
    });

    // Endpoint for MATLAB
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
        int micVal = analogRead(contactMicPin);
        request->send(200, "text/plain", String(micVal));
    });

    // Endpoint for external counter update (e.g., from MATLAB)
    server.on("/updateCounter", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("counter", true)) {
            String counterValue = request->getParam("counter", true)->value();
            eventCounter = counterValue.toInt();
            Serial.println("🔄 Counter updated from external source: " + counterValue);
            request->send(200, "text/plain", "Counter received");
        } else {
            request->send(400, "text/plain", "Missing counter parameter");
        }
    });

    server.begin();

    // Start sensor sampling task
    xTaskCreatePinnedToCore(
        sampleSensors,
        "SampleSensors",
        10000,
        NULL,
        1,
        NULL,
        1
    );
}

void loop() {
    ws.cleanupClients();
}
