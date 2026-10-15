
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#define SDA_GPIO 21
#define SCL_GPIO 22
#define SENSOR_ADDR 0x57

#define REG_WR_PTR      0x04
#define REG_OVF         0x05
#define REG_RD_PTR      0x06
#define REG_FIFO        0x07
#define REG_FIFO_CONFIG 0x08
#define REG_MODE_CONFIG 0x09
#define REG_SPO2_CONFIG 0x0A
#define REG_RED_LED     0x0C
#define REG_IR_LED      0x0D
#define REG_PART_ID     0xFF

#define SAMPLE_RATE_HZ 100
#define WINDOW_SIZE    400
#define REPORT_EVERY   100

static const char *TAG = "VITALHOP";
static i2c_master_dev_handle_t sensor;

static uint32_t red_buf[WINDOW_SIZE];
static uint32_t ir_buf[WINDOW_SIZE];
static float pulse_buf[WINDOW_SIZE];

static uint32_t total_samples = 0;
static uint32_t last_report_sample = 0;

static float previous_ir1 = 0.0f;
static float previous_ir2 = 0.0f;
static float last_peak_index = 0.0f;
static bool have_peak = false;
static int bpm = 0;

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[] = {reg, value};
    return i2c_master_transmit(sensor, data, sizeof(data), 100);
}

static esp_err_t read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(
        sensor, &reg, 1, value, 1, 100);
}

static esp_err_t init_sensor(void)
{
    uint8_t id = 0;

    ESP_RETURN_ON_ERROR(
        read_reg(REG_PART_ID, &id), TAG, "ID read failed");

    ESP_LOGI(TAG, "Part ID = 0x%02X", id);

    if (id != 0x15) {
        ESP_LOGE(TAG, "Unexpected sensor Part ID");
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(
        write_reg(REG_MODE_CONFIG, 0x40), TAG, "Reset failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_RETURN_ON_ERROR(
        write_reg(REG_WR_PTR, 0), TAG, "FIFO setup failed");
    ESP_RETURN_ON_ERROR(
        write_reg(REG_OVF, 0), TAG, "FIFO setup failed");
    ESP_RETURN_ON_ERROR(
        write_reg(REG_RD_PTR, 0), TAG, "FIFO setup failed");

    // One sample per FIFO entry, rollover enabled.
    ESP_RETURN_ON_ERROR(
        write_reg(REG_FIFO_CONFIG, 0x1F),
        TAG, "FIFO configuration failed");

    // ADC range, 100 samples/sec, 411 us pulse width.
    ESP_RETURN_ON_ERROR(
        write_reg(REG_SPO2_CONFIG, 0x27),
        TAG, "SpO2 configuration failed");

    ESP_RETURN_ON_ERROR(
        write_reg(REG_RED_LED, 0x24),
        TAG, "RED LED setup failed");
    ESP_RETURN_ON_ERROR(
        write_reg(REG_IR_LED, 0x24),
        TAG, "IR LED setup failed");

    ESP_RETURN_ON_ERROR(
        write_reg(REG_MODE_CONFIG, 0x03),
        TAG, "SpO2 mode setup failed");

    ESP_LOGI(TAG, "MAX30102 configured at 100 Hz");
    return ESP_OK;
}

static bool process_sample(uint32_t red, uint32_t ir)
{
    uint32_t index = total_samples % WINDOW_SIZE;

    red_buf[index] = red;
    ir_buf[index] = ir;

    // Simple 3-sample smoothing for pulse detection.
    float smooth = (float)ir;

    if (total_samples >= 2) {
        smooth = ((float)ir + previous_ir1 + previous_ir2)
                 / 3.0f;
    }

    pulse_buf[index] = smooth;

    // Detect a local maximum using three consecutive samples.
    if (total_samples >= 2) {
        float candidate = previous_ir1;

        if (previous_ir1 > previous_ir2 &&
            previous_ir1 >= smooth) {

            uint32_t peak_index = total_samples - 1;

            if (!have_peak) {
                last_peak_index = (float)peak_index;
                have_peak = true;
            } else {
                uint32_t interval =
                    peak_index - (uint32_t)last_peak_index;

                // Ignore peaks that are too close together.
                if (interval >= 30) {
                    if (interval <= 200) {
                        bpm = (int)(
                            (60.0f * SAMPLE_RATE_HZ) /
                            (float)interval + 0.5f);
                    }

                    last_peak_index = (float)peak_index;
                }
            }
        }

        (void)candidate;
    }

    previous_ir2 = previous_ir1;
    previous_ir1 = smooth;
    total_samples++;

    return total_samples >= WINDOW_SIZE;
}

static bool calculate_spo2(float *spo2_out)
{
    uint32_t min_red = 0xFFFFFFFFU;
    uint32_t max_red = 0;
    uint32_t min_ir = 0xFFFFFFFFU;
    uint32_t max_ir = 0;

    uint64_t sum_red = 0;
    uint64_t sum_ir = 0;

    for (int i = 0; i < WINDOW_SIZE; i++) {
        uint32_t r = red_buf[i];
        uint32_t ir = ir_buf[i];

        if (r < min_red) min_red = r;
        if (r > max_red) max_red = r;
        if (ir < min_ir) min_ir = ir;
        if (ir > max_ir) max_ir = ir;

        sum_red += r;
        sum_ir += ir;
    }

    float dc_red = (float)sum_red / WINDOW_SIZE;
    float dc_ir = (float)sum_ir / WINDOW_SIZE;

    float ac_red = (float)(max_red - min_red);
    float ac_ir = (float)(max_ir - min_ir);

    // Basic finger-presence and signal-amplitude checks.
    if (dc_ir < 10000.0f || dc_ir > 250000.0f ||
        dc_red < 10000.0f || dc_red > 250000.0f ||
        ac_red < 50.0f || ac_ir < 50.0f) {
        return false;
    }

    // Ratio-of-ratios approximation.
    float ratio = (ac_red / dc_red) / (ac_ir / dc_ir);

    if (!isfinite(ratio) || ratio < 0.1f || ratio > 2.0f) {
        return false;
    }

    // Experimental empirical curve, NOT medical-grade.
    float spo2 = -45.060f * ratio * ratio
                 + 30.354f * ratio
                 + 94.845f;

    if (spo2 < 70.0f) spo2 = 70.0f;
    if (spo2 > 100.0f) spo2 = 100.0f;

    *spo2_out = spo2;
    return true;
}

void app_main(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SDA_GPIO,
        .scl_io_num = SCL_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SENSOR_ADDR,
        .scl_speed_hz = 100000,
    };

    ESP_ERROR_CHECK(
        i2c_master_bus_add_device(bus, &dev_cfg, &sensor));

    if (init_sensor() != ESP_OK) {
        ESP_LOGE(TAG, "Sensor initialization failed");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Keep your finger still on the sensor");

    while (1) {
        uint8_t wr = 0;
        uint8_t rd = 0;

        if (read_reg(REG_WR_PTR, &wr) != ESP_OK ||
            read_reg(REG_RD_PTR, &rd) != ESP_OK) {
            ESP_LOGW(TAG, "FIFO pointer read failed");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        uint8_t count = (uint8_t)((wr - rd) & 0x1F);

        for (uint8_t i = 0; i < count; i++) {
            uint8_t reg = REG_FIFO;
            uint8_t b[6] = {0};

            esp_err_t err = i2c_master_transmit_receive(
                sensor, &reg, 1, b, sizeof(b), 100);

            if (err != ESP_OK) {
                ESP_LOGW(TAG, "FIFO read failed");
                break;
            }

            uint32_t red =
                (((uint32_t)b[0] & 0x03U) << 16) |
                ((uint32_t)b[1] << 8) | b[2];

            uint32_t ir =
                (((uint32_t)b[3] & 0x03U) << 16) |
                ((uint32_t)b[4] << 8) | b[5];

            bool ready = process_sample(red, ir);

            if (ready &&
                total_samples - last_report_sample >= REPORT_EVERY) {

                last_report_sample = total_samples;

                float spo2 = 0.0f;
                bool spo2_valid = calculate_spo2(&spo2);

                if (spo2_valid && bpm > 0) {
                    printf("BPM: %d | Experimental SpO2: %.1f%%\n",
                           bpm, (double)spo2);
                } else if (spo2_valid) {
                    printf("BPM: detecting... | Experimental SpO2: %.1f%%\n",
                           (double)spo2);
                } else {
                    printf("BPM: %s | SpO2: signal unreliable\n",
                           bpm > 0 ? "detected" : "detecting...");
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
