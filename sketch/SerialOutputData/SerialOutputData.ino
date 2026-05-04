/*
 * ESP32 Serial Data Transmitter
 * Sends text, random floats, and sine wave data via Serial at 115200 baud
 */

#include <Arduino.h>
#include <math.h>

void sendTestText() {
  static int count = 0;
  for (int i = 0; i < 400; i++) {
    Serial.print("Serial terminal line ");
    Serial.println(count++);
    delay(100);
    if (i % 25 == 0) delay(2000);
  }
}

// ─────────────────────────────────────────────
Send random floats[-500, +500]
  // ─────────────────────────────────────────────
  void
  sendRandomFloats() {
  // Serial.println("=== Function 2: 100 Random Floats [-500.00, +500.00] ===");

  for (int i = 0; i < 200; i++) {
    // esp_random() gives a full 32-bit unsigned random value
    float value = ((float)esp_random() / (float)UINT32_MAX) * 1000.0f - 500.0f;
    //Serial.printf("%d: %.2f\n", i + 1, value);
    Serial.println(value);
    delay(5);
  }
  delay(500);
}

void sendRamp() {
  for (int i = 0; i < 20; i++) {
    Serial.println(20);
    delay(1);
    Serial.println(40);
    delay(1);
  }
  delay(500);
}

// ─────────────────────────────────────────────
// Sine wave — constant amplitude
// ─────────────────────────────────────────────
void sendSineWave() {
  /*
  Serial.println("=== Function 3: Sine Wave (Constant Amplitude ±500) ===");
  Serial.println("Format: sample_index, time_ms, value");
*/

  const int cycles = 3;
  const float frequency = 5.0f;  // Hz
  const float amplitude = 500.0f;
  const int samplesPerCycle = 100;
  const int totalSamples = cycles * samplesPerCycle;

  // Period = 1/f seconds; sample interval in seconds
  const float sampleInterval = 1.0f / (frequency * samplesPerCycle);

  for (int i = 0; i < totalSamples; i++) {
    float t = i * sampleInterval;  // seconds
    float value = amplitude * sinf(2.0f * M_PI * frequency * t);
    //Serial.printf("%d, %.4f, %.4f\n", i, t * 1000.0f, value); // time in ms
    Serial.println(value);
    delay(10);
  }
}

// ─────────────────────────────────────────────
// Sine wave — linearly decaying amplitude
// ─────────────────────────────────────────────
void sendDecayingSineWave() {
  /*
  Serial.println("=== Function 4: Sine Wave (Decaying Amplitude ±500 → ±50) ===");
  Serial.println("Format: sample_index, time_ms, cycle, amplitude, value");
*/
  const int cycles = 10;
  const float frequency = 5.0f;
  const float ampStart = 500.0f;
  const float ampEnd = 1.0f;
  const int samplesPerCycle = 100;
  const int totalSamples = cycles * samplesPerCycle;

  const float sampleInterval = 1.0f / (frequency * samplesPerCycle);

  for (int i = 0; i < totalSamples; i++) {
    int cycle = i / samplesPerCycle;  // 0-based cycle index

    // Linear interpolation: cycle 0 → ampStart, cycle (cycles-1) → ampEnd
    float t_norm = (cycles > 1) ? (float)cycle / (float)(cycles - 1) : 0.0f;
    float amp = ampStart + t_norm * (ampEnd - ampStart);

    float t = i * sampleInterval;
    float value = amp * sinf(2.0f * M_PI * frequency * t);

    // Serial.printf("%d, %.4f, %d, %.2f, %.4f\n", i, t * 1000.0f, cycle + 1, amp, value);
    Serial.println(value);
    delay(5);
  }
  delay(500);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32 Serial Data Transmitter");
  Serial.println();

  sendRamp();
  sendTestText();
  delay(500);
}

void loop() {

  sendRandomFloats();
  sendSineWave();
  sendDecayingSineWave();
}
