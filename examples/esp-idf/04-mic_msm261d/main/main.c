#include <string.h>
#include "sdkconfig.h"
#include <stdlib.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "driver/i2s_pdm.h"
#include "driver/gpio.h"
#include "format_wav.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

/* I2S port and GPIOs */
#define CLK_GPIO    (GPIO_NUM_26)  // PDM CLK（时钟）
#define DATA_GPIO   (GPIO_NUM_27)  // PDM DATA（数据）

/* SD card GPIOs */
#define EXAMPLE_SD_CMD_IO      (44) 
#define EXAMPLE_SD_CLK_IO      (43)
#define EXAMPLE_SD_DAT0_IO     (39)
#define EXAMPLE_SD_DAT1_IO     (40) 
#define EXAMPLE_SD_DAT2_IO     (41)   
#define EXAMPLE_SD_DAT3_IO     (42) 

/* I2S configurations */
#define SAMPLE_RATE         16000   // 采样率16kHz
#define BITS_PER_SAMPLE     I2S_DATA_BIT_WIDTH_16BIT // 改为16位，便于对齐和试听
#define I2S_CHAN_NUM       (1)   // 单声道

/* SD card & recording configurations */
#define EXAMPLE_RECORD_TIME_SEC    (10)
#define MOUNT_POINT     "/sdcard"
#define EXAMPLE_RECORD_FILE_PATH   "/RECORD.WAV"

static const char *TAG = "example";

i2s_chan_handle_t rx_chan = NULL; // I²S接收通道
sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

static i2s_chan_handle_t i2s_init(void) {
    // I²S通道配置
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan));

    // PDM模式配置（MSM261D是PDM麦克风）
    // PDM时钟频率通常是采样率的64倍（16kHz * 64 = 1.024MHz）
    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = CLK_GPIO,
            .din = DATA_GPIO,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    
    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(rx_chan, &pdm_rx_cfg));
    // 注意：不在初始化时启用，而是在检测/录音时启用
    ESP_LOGI(TAG, "MSM261D PDM麦克风初始化完成");
    return rx_chan;
}

sdmmc_card_t * mount_sdcard(void)
{
    sdmmc_card_t *sdmmc_card = NULL;

    ESP_LOGI(TAG, "Mounting SD card");
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI(TAG, "Initializing SD card");
    ESP_LOGI(TAG, "Using SDMMC peripheral");

    sdmmc_host_t sdmmc_host = SDMMC_HOST_DEFAULT(); // SDMMC主机接口配置
    sdmmc_host.pwr_ctrl_handle = pwr_ctrl_handle;
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT(); // SDMMC插槽配置
    slot_config.width = 4;  // 改为4线SD模式
    slot_config.clk = EXAMPLE_SD_CLK_IO; 
    slot_config.cmd = EXAMPLE_SD_CMD_IO;
    slot_config.d0 = EXAMPLE_SD_DAT0_IO;
    slot_config.d1 = EXAMPLE_SD_DAT1_IO;  // 添加DAT1配置
    slot_config.d2 = EXAMPLE_SD_DAT2_IO;  // 添加DAT2配置
    slot_config.d3 = EXAMPLE_SD_DAT3_IO;  // 添加DAT3配置
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP; // 打开内部上拉电阻

    ESP_LOGI(TAG, "Mounting filesystem");

    esp_err_t ret;
    while (1) {
        ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &sdmmc_host, &slot_config, &mount_config, &sdmmc_card);
        if (ret == ESP_OK) {
            break;
        } else if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). ", esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "Card size: %lluMB, speed: %dMHz",
            (((uint64_t)sdmmc_card->csd.capacity) * sdmmc_card->csd.sector_size) >> 20,
            sdmmc_card->max_freq_khz / 1000);

    return sdmmc_card;
}

// 检测麦克风是否接收到数据（不录音）
static esp_err_t detect_mic_data(i2s_chan_handle_t i2s_rx_chan)
{
    ESP_RETURN_ON_FALSE(i2s_rx_chan, ESP_FAIL, TAG, "invalid i2s channel handle pointer");
    esp_err_t ret = ESP_OK;

    // 24位数据，每个样本3字节，使用字节缓冲区读取原始数据
    #define BUFFER_SIZE 4096
    static uint8_t i2s_readraw_buff[BUFFER_SIZE];
    
    // 检测阈值：24位数据的有效范围是-8388608到8388607，设置阈值为1000
    const int32_t DETECTION_THRESHOLD = 1000;
    
    uint32_t total_samples = 0;
    uint32_t samples_with_data = 0;
    int32_t max_amplitude = 0;
    int32_t min_amplitude = 0;
    
    uint32_t start_time = xTaskGetTickCount();
    uint32_t detection_time_ms = EXAMPLE_RECORD_TIME_SEC * 1000;
    
    ESP_LOGI(TAG, "开始检测麦克风数据，持续 %d 秒...", EXAMPLE_RECORD_TIME_SEC);
    ESP_GOTO_ON_ERROR(i2s_channel_enable(i2s_rx_chan), err, TAG, "error while starting i2s rx channel");
    
    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(detection_time_ms)) {
        size_t bytes_read = 0;
        
        // 读取I2S原始字节数据
        ret = i2s_channel_read(i2s_rx_chan, i2s_readraw_buff, BUFFER_SIZE, &bytes_read, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "读取I2S数据失败: %s", esp_err_to_name(ret));
            continue;
        }
        
        if (bytes_read == 0) {
            continue;
        }
        
        // 解析24位样本（每个样本3字节，MSB first）
        // 注意：I2S 24位数据通常是左对齐的，在32位字中，需要右移8位
        uint32_t samples_read = bytes_read / 4;  // I2S通常返回32位对齐的数据
        total_samples += samples_read;
        
        // 分析数据，检测是否有有效信号
        for (uint32_t i = 0; i < samples_read; i++) {
            // 从32位数据中提取24位样本（左对齐，右移8位）
            int32_t raw_sample = ((int32_t*)i2s_readraw_buff)[i];
            
            // 24位数据在32位中是左对齐的，右移8位得到24位有符号数
            int32_t sample = raw_sample >> 8;
            
            // 计算绝对值
            int32_t abs_sample = (sample < 0) ? -sample : sample;
            
            // 更新最大/最小幅值
            if (abs_sample > max_amplitude) {
                max_amplitude = abs_sample;
            }
            if (sample < min_amplitude) {
                min_amplitude = sample;
            }
            
            // 如果幅值超过阈值，认为检测到数据
            if (abs_sample > DETECTION_THRESHOLD) {
                samples_with_data++;
            }
        }
        
        // 每秒输出一次进度
        uint32_t elapsed_sec = (xTaskGetTickCount() - start_time) / configTICK_RATE_HZ;
        static uint32_t last_sec = 0;
        if (elapsed_sec != last_sec) {
            last_sec = elapsed_sec;
            ESP_LOGI(TAG, "检测中: %"PRIu32"/%ds, 最大幅值: %"PRId32", 有数据样本: %"PRIu32"/%"PRIu32, 
                     elapsed_sec, EXAMPLE_RECORD_TIME_SEC, max_amplitude, samples_with_data, total_samples);
        }
    }

err:
    i2s_channel_disable(i2s_rx_chan);
    
    // 输出检测结果
    ESP_LOGI(TAG, "========== 检测结果 ==========");
    ESP_LOGI(TAG, "总样本数: %"PRIu32, total_samples);
    ESP_LOGI(TAG, "有数据样本数: %"PRIu32, samples_with_data);
    ESP_LOGI(TAG, "最大幅值: %"PRId32, max_amplitude);
    ESP_LOGI(TAG, "最小幅值: %"PRId32, min_amplitude);
    
    if (samples_with_data > 0) {
        float data_ratio = (float)samples_with_data / total_samples * 100.0f;
        ESP_LOGI(TAG, "数据占比: %.2f%%", data_ratio);
        ESP_LOGI(TAG, "✓ 检测到麦克风数据！");
    } else {
        ESP_LOGW(TAG, "✗ 未检测到有效数据（可能是静音或连接问题）");
    }
    ESP_LOGI(TAG, "==============================");

    return ret;
}

static esp_err_t record_wav(i2s_chan_handle_t i2s_rx_chan)
{
    ESP_RETURN_ON_FALSE(i2s_rx_chan, ESP_FAIL, TAG, "invalid i2s channel handle pointer");
    esp_err_t ret = ESP_OK;

    uint32_t byte_rate = SAMPLE_RATE * I2S_CHAN_NUM * BITS_PER_SAMPLE / 8;
    uint32_t wav_size = byte_rate * EXAMPLE_RECORD_TIME_SEC;

    const wav_header_t wav_header =
        WAV_HEADER_PCM_DEFAULT(wav_size, 16, SAMPLE_RATE, I2S_CHAN_NUM); // 16位WAV头

    ESP_LOGI(TAG, "Opening file %s", EXAMPLE_RECORD_FILE_PATH);
    FILE *f = fopen(MOUNT_POINT EXAMPLE_RECORD_FILE_PATH, "wb");  // 使用二进制模式
    ESP_RETURN_ON_FALSE(f, ESP_FAIL, TAG, "error while opening wav file");

    /* Write wav header */
    ESP_GOTO_ON_FALSE(fwrite(&wav_header, sizeof(wav_header_t), 1, f), ESP_FAIL, err,
                      TAG, "error while writing wav header");

    /* Start recording */
    size_t wav_written = 0;
    // 使用16位缓冲区读取I2S数据
    #define I2S_BUFFER_SAMPLES 1024
    static int16_t i2s_readraw_buff[I2S_BUFFER_SAMPLES];
    size_t i2s_buffer_size = sizeof(i2s_readraw_buff);
    
    ESP_GOTO_ON_ERROR(i2s_channel_enable(i2s_rx_chan), err, TAG, "error while starting i2s rx channel");
    
    int log_count = 0; // 仅前几次打印样本
    while (wav_written < wav_size) {
        if(wav_written % byte_rate < i2s_buffer_size) {
            ESP_LOGI(TAG, "Recording: %"PRIu32"/%ds", wav_written/byte_rate + 1, EXAMPLE_RECORD_TIME_SEC);
        }
        size_t bytes_read = 0;
        
        /* Read RAW samples from I2S (16-bit samples) */
        ret = i2s_channel_read(i2s_rx_chan, i2s_readraw_buff, i2s_buffer_size, &bytes_read,
                              pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "读取I2S数据失败: %s", esp_err_to_name(ret));
            continue;
        }
        
        if (bytes_read < 2) { // 需要至少1个16位样本
            continue;
        }
        
        // 计算读取的样本数（每个样本2字节，16位）
        uint32_t samples_read = bytes_read / 2;
        
        // 仅前几次打印详细统计信息
        if (log_count < 50) {
            int16_t s0 = samples_read > 0 ? i2s_readraw_buff[0] : 0;
            int16_t s1 = samples_read > 1 ? i2s_readraw_buff[1] : 0;
            int16_t s2 = samples_read > 2 ? i2s_readraw_buff[2] : 0;
            int16_t s3 = samples_read > 3 ? i2s_readraw_buff[3] : 0;
            int16_t s4 = samples_read > 4 ? i2s_readraw_buff[4] : 0;
            
            // 计算统计信息（使用int64避免溢出）
            int64_t sum = 0;
            int16_t min_val = 32767, max_val = -32768;
            int64_t abs_sum = 0;
            uint32_t stat_count = (samples_read < 100) ? samples_read : 100;
            for (uint32_t i = 0; i < stat_count; i++) {
                int16_t val = i2s_readraw_buff[i];
                sum += (int64_t)val;
                abs_sum += (val < 0 ? -(int64_t)val : (int64_t)val);
                if (val < min_val) min_val = val;
                if (val > max_val) max_val = val;
            }
            int32_t avg = stat_count > 0 ? (int32_t)(sum / stat_count) : 0;
            int32_t abs_avg = stat_count > 0 ? (int32_t)(abs_sum / stat_count) : 0;
            
            ESP_LOGI(TAG, "i2s read bytes=%d samples=%d first5=[%d,%d,%d,%d,%d]",
                     (int)bytes_read, (int)samples_read, s0, s1, s2, s3, s4);
            ESP_LOGI(TAG, "  stats: min=%d max=%d avg=%ld abs_avg=%ld (接近0=静音, 接近32767=满幅噪声)",
                     min_val, max_val, (long)avg, (long)abs_avg);
            log_count++;
        }

        // PDM模式返回的数据已经是解码后的16位PCM，直接写入
        size_t bytes_to_write = samples_read * 2;
        if (bytes_to_write == 0) {
            continue;
        }
        
        // 确保不超过剩余需要写入的字节数
        if (wav_written + bytes_to_write > wav_size) {
            bytes_to_write = wav_size - wav_written;
            // 如果截断，需要调整samples_read
            samples_read = bytes_to_write / 2;
            bytes_to_write = samples_read * 2;
        }
        
        /* Write the 16-bit PCM samples to the WAV file */
        ESP_GOTO_ON_FALSE(fwrite(i2s_readraw_buff, bytes_to_write, 1, f), ESP_FAIL, err,
                          TAG, "error while writing samples to wav file");
        wav_written += bytes_to_write;
    }

err:
    i2s_channel_disable(i2s_rx_chan);
    ESP_LOGI(TAG, "Recording done! Flushing file buffer");
    fclose(f);

    return ret;
}

void app_main(void)
{
    esp_err_t ret;

    /* 初始化I2S接口 */
    i2s_chan_handle_t i2s_rx_chan = i2s_init();  

    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4,
    };

    ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create a new on-chip LDO power control driver");
        return;
    }

    /* 挂载SD卡 */
    sdmmc_card_t *sdmmc_card = mount_sdcard();
    /* 检测麦克风数据（不录音）*/
    // ESP_LOGI(TAG, "开始检测麦克风数据...");
    // ret = detect_mic_data(i2s_rx_chan);
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "检测失败");
    // } else {
    //     ESP_LOGI(TAG, "检测完成");
    // }
    /* 录音 */
    ESP_LOGI(TAG, "开始录音...");
    ret = record_wav(i2s_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "录音失败");
    } else {
        ESP_LOGI(TAG, "录音完成");
    }
    /* 弹出SD卡 */
    ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, sdmmc_card);
    if(ret == ESP_OK) {
        ESP_LOGI(TAG, "Audio was successfully recorded into "EXAMPLE_RECORD_FILE_PATH
                      ". You can now remove the SD card safely");
    } else {
        ESP_LOGE(TAG, "Record failed, "EXAMPLE_RECORD_FILE_PATH" on SD card may not be playable.");
    }

    ret = sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete the on-chip LDO power control driver");
        return;
    }
}
