#include "driver/pcnt.h"

#define SIGNAL_PIN 4
#define PCNT_UNIT PCNT_UNIT_0
#define PCNT_CHANNEL PCNT_CHANNEL_0

#define CALIBRATION_MS 2000
#define DETECTION_THRESHOLD 0.015f  // 1%
#define SAMPLE_INTERVAL_MS 2       // high-resolution sampling
#define EMA_ALPHA 0.2f             // smoothing factor
#define PRINT_INTERVAL_MS 500      // update print every 500ms

// --- Calibration and EMA ---
float calibratedHighFreq = 0;
float calibratedLowFreq  = 0;

float freqHighEMA = 0;
float freqLowEMA  = 0;

// --- PCNT setup ---
void setupPCNT() {
    pcnt_config_t pcnt_cfg;
    pcnt_cfg.pulse_gpio_num = SIGNAL_PIN;
    pcnt_cfg.ctrl_gpio_num  = PCNT_PIN_NOT_USED;
    pcnt_cfg.channel        = PCNT_CHANNEL;
    pcnt_cfg.unit           = PCNT_UNIT;

    pcnt_cfg.pos_mode  = PCNT_COUNT_INC;   
    pcnt_cfg.neg_mode  = PCNT_COUNT_DIS;   
    pcnt_cfg.lctrl_mode = PCNT_MODE_KEEP;
    pcnt_cfg.hctrl_mode = PCNT_MODE_KEEP;

    pcnt_cfg.counter_h_lim = INT16_MAX;
    pcnt_cfg.counter_l_lim = INT16_MIN;

    pcnt_unit_config(&pcnt_cfg);
    pcnt_counter_pause(PCNT_UNIT);
    pcnt_counter_clear(PCNT_UNIT);
    pcnt_counter_resume(PCNT_UNIT);
}

// --- Calibration ---
void calibrateFrequency() {
    Serial.println("Calibrating HIGH/LOW frequencies...");
    unsigned long start = millis();
    int64_t sumHigh = 0, sumLow = 0;
    int cntHigh = 0, cntLow = 0;
    bool isHigh = false;

    while (millis() - start < CALIBRATION_MS) {
        delay(50);
        int16_t count;
        pcnt_get_counter_value(PCNT_UNIT, &count);
        pcnt_counter_clear(PCNT_UNIT);

        float freq = count * (1000.0f / 50.0f);

        if (isHigh) { sumHigh += freq; cntHigh++; }
        else        { sumLow  += freq; cntLow++; }

        isHigh = !isHigh;
    }

    calibratedHighFreq = cntHigh ? sumHigh / cntHigh : 0;
    calibratedLowFreq  = cntLow  ? sumLow  / cntLow  : 0;

    freqHighEMA = calibratedHighFreq;
    freqLowEMA  = calibratedLowFreq;

    Serial.printf("Calibration complete. HIGH=%.2f Hz, LOW=%.2f Hz\n", calibratedHighFreq, calibratedLowFreq);
}

// --- Setup ---
void setup() {
    Serial.begin(115200);
    pinMode(SIGNAL_PIN, INPUT);

    setupPCNT();
    calibrateFrequency();
}

// --- Loop ---
void loop() {
    static unsigned long lastSample = 0;
    static unsigned long lastPrint = 0;
    static bool isHigh = false;
    static bool metalDetectedHigh = false;
    static bool metalDetectedLow  = false;

    unsigned long now = millis();

    // High-resolution sampling
    if (now - lastSample >= SAMPLE_INTERVAL_MS) {
        lastSample = now;

        int16_t count;
        pcnt_get_counter_value(PCNT_UNIT, &count);
        pcnt_counter_clear(PCNT_UNIT);

        float freq = count * (1000.0f / SAMPLE_INTERVAL_MS);

        // --- Update EMA per phase ---
        if (isHigh) {
            freqHighEMA = EMA_ALPHA * freq + (1 - EMA_ALPHA) * freqHighEMA;
            metalDetectedHigh = (freqHighEMA > calibratedHighFreq * (1.0f + DETECTION_THRESHOLD));
        } else {
            freqLowEMA = EMA_ALPHA * freq + (1 - EMA_ALPHA) * freqLowEMA;
            metalDetectedLow = (freqLowEMA > calibratedLowFreq * (1.0f + DETECTION_THRESHOLD));
        }

        isHigh = !isHigh;
    }

    // Print update every 500ms
    if (now - lastPrint >= PRINT_INTERVAL_MS) {
        lastPrint = now;
        Serial.printf("HIGH EMA: %.2f Hz%s | LOW EMA: %.2f Hz%s\n",
                      freqHighEMA, metalDetectedHigh ? " [Metal!]" : "",
                      freqLowEMA,  metalDetectedLow  ? " [Metal!]" : "");
    }
}
