
#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#define SDA_GPIO 21
#define SCL_GPIO 22
#define SENSOR_ADDR 0x57

#define REG_WR_PTR 0x04
#define REG_OVF    0x05
#define REG_RD_PTR 0x06
#define REG_FIFO   0x07
#define REG_FIFO_CONFIG 0x08
#define REG_MODE_CONFIG 0x09
#define REG_SPO2_CONFIG 0x0A
#define REG_RED_LED 0x0C
#define REG_IR_LED 0x0D
#define REG_PART_ID 0xFF

static const char *TAG = "MAX30102";
static i2c_master_dev_handle_t sensor;

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[] = {reg, value};
    return i2c_master_transmit(sensor, data, sizeof(data), 100);
}

static esp_err_t read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(sensor, &reg, 1, value, 1, 100);
}

static esp_err_t init_sensor(void)
{
    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(read_reg(REG_PART_ID, &id), TAG, "ID read failed");
    ESP_LOGI(TAG, "Part ID = 0x%02X", id);

    if (id != 0x15) {
        ESP_LOGE(TAG, "Unexpected Part ID; check sensor and wiring");
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(write_reg(REG_MODE_CONFIG, 0x40), TAG, "Reset failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_RETURN_ON_ERROR(write_reg(REG_WR_PTR, 0), TAG, "FIFO setup failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_OVF, 0), TAG, "FIFO setup failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_RD_PTR, 0), TAG, "FIFO setup failed");

    ESP_RETURN_ON_ERROR(write_reg(REG_FIFO_CONFIG, 0x4F), TAG, "FIFO config failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_SPO2_CONFIG, 0x27), TAG, "SpO2 config failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_RED_LED, 0x24), TAG, "RED LED setup failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_IR_LED, 0x24), TAG, "IR LED setup failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_MODE_CONFIG, 0x03), TAG, "Mode setup failed");

    ESP_LOGI(TAG, "Sensor configured for raw RED/IR readings");
    return ESP_OK;
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

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &sensor));

    if (init_sensor() != ESP_OK) {
        ESP_LOGE(TAG, "Initialization failed; check power and wiring");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    while (1) {
        uint8_t wr = 0, rd = 0;
        if (read_reg(REG_WR_PTR, &wr) != ESP_OK ||
            read_reg(REG_RD_PTR, &rd) != ESP_OK) {
            ESP_LOGW(TAG, "FIFO pointer read failed");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        uint8_t count = (uint8_t)((wr - rd) & 0x1F);
        for (uint8_t i = 0; i < count; i++) {
            uint8_t reg = REG_FIFO;
            uint8_t b[6] = {0};
            esp_err_t err = i2c_master_transmit_receive(
                sensor, &reg, 1, b, sizeof(b), 100);

            if (err != ESP_OK) {
                ESP_LOGW(TAG, "FIFO read failed: %s", esp_err_to_name(err));
                break;
            }

            uint32_t red = (((uint32_t)b[0] & 3U) << 16) |
                           ((uint32_t)b[1] << 8) | b[2];
            uint32_t ir = (((uint32_t)b[3] & 3U) << 16) |
                          ((uint32_t)b[4] << 8) | b[5];

            printf("Raw readings: RED=%lu IR=%lu\n",
                   (unsigned long)red, (unsigned long)ir);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
