#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "VITALHOP_PATIENT";

void app_main(void)
{
    ESP_LOGI(TAG, "VitalHop patient node started");
    ESP_LOGI(TAG, "Stage 0: Hardware bring-up");
    ESP_LOGI(TAG, "Sensors and LoRa not initialized yet");

    uint32_t count = 0;

    while (1) {
        ESP_LOGI(TAG,
                 "Heartbeat: count=%" PRIu32
                 ", uptime_ms=%" PRId64,
                 count++,
                 esp_timer_get_time() / 1000);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
