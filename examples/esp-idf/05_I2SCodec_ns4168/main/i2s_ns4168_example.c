#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/gpio_filter.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "ns4168.h"
#include "driver/i2c_master.h"
#include "driver/pulse_cnt.h"
#include <math.h>

static char *TAG = "audio";

// 音频缓冲区大小
#define BUFFER_SIZE 2048
#define VOLUME 0.6f // 60%音量

i2c_master_bus_handle_t i2c_bus = NULL;

// 正弦波生成任务
void sine_wave_task(void *pvParameters)
{
    int16_t *audio_buffer = (int16_t *)malloc(512 * sizeof(int16_t));
    if (audio_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        vTaskDelete(NULL);
        return;
    }
    
    size_t bytes_written;
    float phase = 0.0f;
    const float phase_increment = 2.0f * M_PI * 440.0f / NS4168_SAMPLE_RATE; // 使用正确的采样率
    
    ESP_LOGI(TAG, "开始播放440Hz正弦波");
    
    while (1) {
        // 生成正弦波
        for (int i = 0; i < 512; i++) {
            audio_buffer[i] = (int16_t)(sinf(phase) * 10000.0f * VOLUME);
            phase += phase_increment;
            if (phase > 2.0f * M_PI) {
                phase -= 2.0f * M_PI;
            }
        }
        
        // 发送到NS4168
        esp_err_t ret = i2s_channel_write(tx_chan, audio_buffer, 512 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write to I2S: %s", esp_err_to_name(ret));
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    free(audio_buffer);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "应用启动");
    ns4168_gpio_init();
    ns4168_i2s_init();

    // 创建正弦波任务
    xTaskCreate(sine_wave_task, "sine_wave", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "音频回环任务已启动");
}
