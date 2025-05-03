#include <WiFi.h>               // Wi-Fi control
#include <AsyncTCP.h>           // Asynchronous TCP (required for web server)
#include <ESPAsyncWebSrv.h>     // Asynchronous web server
#include <math.h>               // Math functions

// ==== Wi-Fi CREDENTIALS ====
const char* ssid = "iPhone";            // Your hotspot or router SSID
const char* password = "mattmia7";      // Your Wi-Fi password

// ==== SENSOR PINS ====
const int xPin = 34;                    // Accelerometer X-axis (analog)
const int yPin = 35;                    // Accelerometer Y-axis (analog)
const int zPin = 32;                    // Accelerometer Z-axis (analog)
const int emgPin = 36;                  // EMG sensor (analog)
const int contactMicPin = 33;           // Contact microphone (analog)
const int thermistorPin = 39;           // Thermistor input (analog)

// ==== THRESHOLD CONSTANTS ====
const int contactThreshold = 2000;      // Raw contact mic threshold
const float seriesResistor = 10000.0;   // Series resistor in voltage divider with thermistor
const float wearThreshold = 8500.0;     // Resistance threshold to determine "worn"

// ==== GLOBAL VARIABLES ====
AsyncWebServer server(80);              // HTTP server on port 80
AsyncWebSocket ws("/ws");              // WebSocket endpoint at /ws

unsigned long lastSampleTime = 0;       // Timestamp of last sensor read
int emgValue = 0;                       
int contactMicValue = 0;
bool isWorn = false;                    // Whether device is currently being worn
int eventCounter = 0;                   // Event count (e.g., bruxism/TMJ clicks)
bool wasAboveThreshold = false;         // Used to detect rising edge for threshold crossings

// ==== ORIENTATION FUNCTION ====
String getOrientation() {
    // Read raw ADC values from accelerometer
    int rawX = analogRead(xPin);
    int rawY = analogRead(yPin);
    int rawZ = analogRead(zPin);

    // Convert to voltage (assuming 3.3V reference and 12-bit ADC)
    float voltageX = (rawX / 4095.0) * 3.3;
    float voltageY = (rawY / 4095.0) * 3.3;
    float voltageZ = (rawZ / 4095.0) * 3.3;

    // Convert voltage to acceleration (centered at 1.5V, sensitivity ~60mV/g)
    float accelX = (voltageX - 1.5) / 0.06;
    float accelY = (voltageY - 1.5) / 0.06;
    float accelZ = (voltageZ - 1.5) / 0.06;

    // Determine body orientation based on Y/Z axes
    if (accelY > 0.0) return "Laying Down on Left Side";
    if (accelY < -1.2) return "Laying Down on Right Side";
    if (accelZ > 0.2) return "Laying Down on Back";
    if (accelX > 0.0 && accelY > -1.0 && accelZ < 0.0) return "Standing Up";
    return "Unknown Position";
}

// ==== WEAR STATUS DETECTION ====
bool checkWearStatus() {
    int adcValue = analogRead(thermistorPin);
    float voltage = adcValue * (3.3 / 4095.0);
    float resistance = (seriesResistor * (3.3 - voltage)) / voltage;
    return resistance < wearThreshold;  // True = being worn
}

// ==== SENSOR SAMPLING TASK ====
void sampleSensors(void *parameter) {
    char message[128];  // Buffer to hold JSON message

    while (true) {
        unsigned long currentTime = micros();
        if (currentTime - lastSampleTime >= 100000) {  // Sample every 100ms (10Hz)
            lastSampleTime += 100000;

            // Read sensors
            emgValue = analogRead(emgPin);
            contactMicValue = analogRead(contactMicPin);
            isWorn = checkWearStatus();
            String orientation = getOrientation();

            // Event detection (rising edge of threshold crossing)
            if (contactMicValue > contactThreshold && !wasAboveThreshold) {
                eventCounter++;
                wasAboveThreshold = true;
                Serial.println("📈 Contact Mic Event! Counter: " + String(eventCounter));
            } else if (contactMicValue <= contactThreshold) {
                wasAboveThreshold = false;
            }

            // Format sensor data into JSON
            snprintf(message, sizeof(message),
                "{ \"emg\": %d, \"contactMic\": %d, \"orientation\": \"%s\", \"isWorn\": %s, \"counter\": %d }",
                emgValue, contactMicValue, orientation.c_str(), isWorn ? "true" : "false", eventCounter);

            // Broadcast data over WebSocket to all connected clients
            if (ws.count() > 0) {
                ws.textAll(message);
            }

            vTaskDelay(1);  // Brief delay to yield to other tasks
        }
    }
}

// ==== SETUP FUNCTION ====
void setup() {
    Serial.begin(115200);

    // Connect to Wi-Fi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n✅ Connected to WiFi!");
    Serial.println("ESP32 IP Address: " + WiFi.localIP().toString());

    // WebSocket event handling
    ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                  void *arg, uint8_t *data, size_t len) {
        if (type == WS_EVT_DISCONNECT) {
            Serial.println("⚠️ WebSocket Disconnected.");
            ws.cleanupClients();
        }
    });
    server.addHandler(&ws);

    // Serve HTML UI page
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

    // Endpoint to fetch contact mic value (for MATLAB)
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
        int micVal = analogRead(contactMicPin);
        request->send(200, "text/plain", String(micVal));
    });

    // Endpoint to update event counter from an external app (like MATLAB)
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

    server.begin();  // Start the server

    // Launch sensor sampling loop on core 1
    xTaskCreatePinnedToCore(
        sampleSensors,       // Task function
        "SampleSensors",     // Task name
        10000,               // Stack size (bytes)
        NULL,                // Parameters
        1,                   // Priority
        NULL,                // Task handle
        1                    // Core ID
    );
}

// ==== MAIN LOOP ====
void loop() {
    ws.cleanupClients();  // Clean up disconnected WebSocket clients
}
