#ifndef NS4168_H
#define NS4168_H
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
// NS4168引脚映射
#define NS4168_CTRL_PIN    GPIO_NUM_31    // 控制声道和电源
#define NS4168_LRCLK_PIN   GPIO_NUM_32    // LRCLK（WS）
#define NS4168_BCLK_PIN    GPIO_NUM_33    // BCLK（BCK）
#define NS4168_SDATA_PIN   GPIO_NUM_34    // SDATA（DATA_OUT）

// I2S配置参数
#define I2S_PORT           I2S_NUM_0     // 使用I2S0通道
#define NS4168_SAMPLE_RATE        48000         // 采样率48kHz
#define NS4168_BITS_PER_SAMPLE    16            // 16位数据
#define CHANNELS           1             // 立体声
extern i2s_chan_handle_t tx_chan; // I2S tx channel handler
extern i2s_std_config_t std_cfg;
void ns4168_i2s_init(void);
void ns4168_gpio_init(void);
#endif