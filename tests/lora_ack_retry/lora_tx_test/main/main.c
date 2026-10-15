#define LORA_TX_ROLE 1
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#define PIN_MISO    GPIO_NUM_19
#define PIN_MOSI    GPIO_NUM_23
#define PIN_SCLK    GPIO_NUM_18
#define PIN_NSS     GPIO_NUM_5
#define PIN_RESET   GPIO_NUM_14
#define PIN_DIO0    GPIO_NUM_26

#define LORA_SPI_HOST SPI2_HOST
#define LORA_HZ       (1000000)
#define LORA_SPI_HZ   LORA_HZ
#define LORA_FREQ_HZ  433000000UL

/* SX1276/SX1278 register addresses */
#define REG_FIFO                 0x00
#define REG_OP_MODE              0x01
#define REG_FRF_MSB              0x06
#define REG_FRF_MID              0x07
#define REG_FRF_LSB              0x08
#define REG_PA_CONFIG            0x09
#define REG_LNA                  0x0C
#define REG_FIFO_ADDR_PTR        0x0D
#define REG_FIFO_TX_BASE_ADDR    0x0E
#define REG_FIFO_RX_BASE_ADDR    0x0F
#define REG_FIFO_RX_CURRENT_ADDR 0x10
#define REG_IRQ_FLAGS            0x12
#define REG_RX_NB_BYTES          0x13
#define REG_MODEM_CONFIG1        0x1D
#define REG_MODEM_CONFIG2        0x1E
#define REG_PREAMBLE_MSB         0x20
#define REG_PREAMBLE_LSB         0x21
#define REG_PAYLOAD_LENGTH       0x22
#define REG_MODEM_CONFIG3        0x26
#define REG_SYNC_WORD            0x39
#define REG_VERSION              0x42
#define REG_PA_DAC               0x4D

#define MODE_LONG_RANGE_MODE 0x80
#define MODE_SLEEP           0x00
#define MODE_STDBY           0x01
#define MODE_TX              0x03
#define MODE_RX_CONTINUOUS   0x05

#define IRQ_RX_DONE  0x40
#define IRQ_TX_DONE  0x08
#define IRQ_CRC_ERR  0x20

static const char *TAG = "VITALHOP_LORA";
static spi_device_handle_t spi;

static esp_err_t sx_write(uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = { (uint8_t)(reg | 0x80), value };
    spi_transaction_t t = {0};
    t.length = 16;
    t.tx_buffer = tx;
    return spi_device_transmit(spi, &t);
}

static esp_err_t sx_read(uint8_t reg, uint8_t *value)
{
    uint8_t tx[2] = { (uint8_t)(reg & 0x7F), 0x00 };
    uint8_t rx[2] = {0};
    spi_transaction_t t = {0};
    t.length = 16;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    esp_err_t err = spi_device_transmit(spi, &t);
    if (err == ESP_OK) *value = rx[1];
    return err;
}

static esp_err_t sx_burst_write(uint8_t reg, const uint8_t *data, size_t len)
{
    if (len > 255) return ESP_ERR_INVALID_SIZE;
    uint8_t tx[257];
    tx[0] = reg | 0x80;
    memcpy(&tx[1], data, len);
    spi_transaction_t t = {0};
    t.length = (len + 1) * 8;
    t.tx_buffer = tx;
    return spi_device_transmit(spi, &t);
}

static esp_err_t sx_burst_read(uint8_t reg, uint8_t *data, size_t len)
{
    if (len > 255) return ESP_ERR_INVALID_SIZE;
    uint8_t tx[257] = {0};
    uint8_t rx[257] = {0};
    tx[0] = reg & 0x7F;
    spi_transaction_t t = {0};
    t.length = (len + 1) * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    esp_err_t err = spi_device_transmit(spi, &t);
    if (err == ESP_OK) memcpy(data, &rx[1], len);
    return err;
}

static void lora_reset(void)
{
    gpio_set_level(PIN_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
}

static esp_err_t lora_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_NSS) | (1ULL << PIN_RESET),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    ESP_ERROR_CHECK(gpio_set_level(PIN_NSS, 1));
    ESP_ERROR_CHECK(gpio_set_level(PIN_RESET, 1));

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_DIO0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 258
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LORA_SPI_HOST, &bus, SPI_DMA_DISABLED));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = LORA_SPI_HZ,
        .mode = 0,
        .spics_io_num = PIN_NSS,
        .queue_size = 1
    };
    ESP_ERROR_CHECK(spi_bus_add_device(LORA_SPI_HOST, &dev, &spi));

    lora_reset();

    uint8_t version = 0;
    ESP_ERROR_CHECK(sx_read(REG_VERSION, &version));
    printf("SX127x RegVersion: 0x%02X (SX1276/77/78/79 normally reports 0x12)\n", version);
    if (version != 0x12) {
        ESP_LOGE(TAG, "Unexpected radio version. Check power, SPI wiring and module identity.");
        return ESP_FAIL;
    }

    /* Enter LoRa sleep, then standby before configuring LoRa registers. */
    ESP_ERROR_CHECK(sx_write(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(sx_write(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY));
    vTaskDelay(pdMS_TO_TICKS(10));

    /* 433 MHz FRF = 433000000 / 32000000 * 2^19 = 0x6C4000 */
    ESP_ERROR_CHECK(sx_write(REG_FRF_MSB, 0x6C));
    ESP_ERROR_CHECK(sx_write(REG_FRF_MID, 0x40));
    ESP_ERROR_CHECK(sx_write(REG_FRF_LSB, 0x00));

    /* BW 125 kHz, coding rate 4/5, explicit header. */
    ESP_ERROR_CHECK(sx_write(REG_MODEM_CONFIG1, 0x72));
    /* SF7, CRC enabled. */
    ESP_ERROR_CHECK(sx_write(REG_MODEM_CONFIG2, 0x74));
    /* AGC automatic, low-data-rate optimize disabled for this configuration. */
    ESP_ERROR_CHECK(sx_write(REG_MODEM_CONFIG3, 0x04));

    ESP_ERROR_CHECK(sx_write(REG_PREAMBLE_MSB, 0x00));
    ESP_ERROR_CHECK(sx_write(REG_PREAMBLE_LSB, 0x08));
    ESP_ERROR_CHECK(sx_write(REG_SYNC_WORD, 0x12));
    ESP_ERROR_CHECK(sx_write(REG_FIFO_TX_BASE_ADDR, 0x00));
    ESP_ERROR_CHECK(sx_write(REG_FIFO_RX_BASE_ADDR, 0x00));
    ESP_ERROR_CHECK(sx_write(REG_LNA, 0x23)); /* maximum LNA gain, boost */
    ESP_ERROR_CHECK(sx_write(REG_PA_CONFIG, 0x8C)); /* PA_BOOST, about +14 dBm */
    ESP_ERROR_CHECK(sx_write(REG_PA_DAC, 0x84)); /* normal PA DAC mode */
    ESP_ERROR_CHECK(sx_write(REG_IRQ_FLAGS, 0xFF)); /* clear pending IRQs */

    printf("LoRa configured: 433.000 MHz, BW 125 kHz, SF7, CR 4/5, CRC ON\n");
    return ESP_OK;
}

static esp_err_t lora_send(const uint8_t *payload, uint8_t length)
{
    ESP_ERROR_CHECK(sx_write(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY));
    ESP_ERROR_CHECK(sx_write(REG_IRQ_FLAGS, 0xFF));
    ESP_ERROR_CHECK(sx_write(REG_FIFO_ADDR_PTR, 0x00));
    ESP_ERROR_CHECK(sx_burst_write(REG_FIFO, payload, length));
    ESP_ERROR_CHECK(sx_write(REG_PAYLOAD_LENGTH, length));
    ESP_ERROR_CHECK(sx_write(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_TX));

    for (int i = 0; i < 300; i++) {
        uint8_t flags = 0;
        esp_err_t err = sx_read(REG_IRQ_FLAGS, &flags);
        if (err != ESP_OK) return err;
        if (flags & IRQ_TX_DONE) {
            ESP_ERROR_CHECK(sx_write(REG_IRQ_FLAGS, IRQ_TX_DONE));
            ESP_ERROR_CHECK(sx_write(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY));
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_ERROR_CHECK(sx_write(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY));
    return ESP_ERR_TIMEOUT;
}

static esp_err_t lora_start_rx(void)
{
    ESP_ERROR_CHECK(sx_write(REG_IRQ_FLAGS, 0xFF));
    ESP_ERROR_CHECK(sx_write(REG_FIFO_ADDR_PTR, 0x00));
    return sx_write(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS);
}

static bool lora_read_packet(char *out, size_t out_size)
{
    uint8_t flags = 0;
    if (sx_read(REG_IRQ_FLAGS, &flags) != ESP_OK || !(flags & IRQ_RX_DONE)) return false;
    if (flags & IRQ_CRC_ERR) {
        printf("RX CRC error\n");
        (void)sx_write(REG_IRQ_FLAGS, IRQ_RX_DONE | IRQ_CRC_ERR);
        (void)lora_start_rx();
        return false;
    }
    uint8_t addr = 0, len = 0;
    if (sx_read(REG_FIFO_RX_CURRENT_ADDR, &addr) != ESP_OK ||
        sx_read(REG_RX_NB_BYTES, &len) != ESP_OK || len == 0 || len >= out_size) {
        (void)sx_write(REG_IRQ_FLAGS, IRQ_RX_DONE | IRQ_CRC_ERR);
        (void)lora_start_rx();
        return false;
    }
    uint8_t payload[256] = {0};
    (void)sx_write(REG_FIFO_ADDR_PTR, addr);
    if (sx_burst_read(REG_FIFO, payload, len) != ESP_OK) {
        (void)sx_write(REG_IRQ_FLAGS, IRQ_RX_DONE | IRQ_CRC_ERR);
        (void)lora_start_rx();
        return false;
    }
    memcpy(out, payload, len);
    out[len] = '\0';
    (void)sx_write(REG_IRQ_FLAGS, IRQ_RX_DONE | IRQ_CRC_ERR);
    return true;
}

void app_main(void)
{
    printf("\n=== VitalHop LoRa TX: ACK + RETRY TEST ===\n");
    if (lora_init() != ESP_OK) { printf("Radio initialization failed.\n"); return; }

    uint32_t seq = 1;
    while (1) {
        bool acknowledged = false;
        for (int attempt = 1; attempt <= 3 && !acknowledged; attempt++) {
            char message[80];
            snprintf(message, sizeof(message), "DATA|%lu|VITALHOP_ACK_TEST", (unsigned long)seq);
            esp_err_t err = lora_send((const uint8_t *)message, (uint8_t)strlen(message));
            if (err != ESP_OK) {
                printf("TX error for ID %lu: %s\n", (unsigned long)seq, esp_err_to_name(err));
                continue;
            }
            printf("TX DATA id=%lu attempt=%d/3: %s\n", (unsigned long)seq, attempt, message);
            (void)lora_start_rx();
            TickType_t start = xTaskGetTickCount();
            const TickType_t timeout = pdMS_TO_TICKS(1000);
            while ((xTaskGetTickCount() - start) < timeout) {
                char reply[256];
                if (lora_read_packet(reply, sizeof(reply))) {
                    printf("RX reply: %s\n", reply);
                    char expected[32];
                    snprintf(expected, sizeof(expected), "ACK|%lu", (unsigned long)seq);
                    if (strcmp(reply, expected) == 0) {
                        printf("ACK OK id=%lu (attempt %d)\n", (unsigned long)seq, attempt);
                        acknowledged = true;
                        break;
                    }
                    printf("Ignoring non-matching reply; expected %s\n", expected);
                    (void)lora_start_rx();
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (!acknowledged) printf("ACK TIMEOUT id=%lu attempt=%d\n", (unsigned long)seq, attempt);
        }
        if (!acknowledged) printf("DELIVERY FAILED id=%lu after 3 attempts\n", (unsigned long)seq);
        else printf("DELIVERY SUCCESS id=%lu\n", (unsigned long)seq);
        seq++;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
