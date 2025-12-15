#include "ns4168.h"

i2s_chan_handle_t tx_chan;
i2s_std_config_t std_cfg;
// 初始化NS4168的GPIO控制
void ns4168_gpio_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = BIT64(NS4168_CTRL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(NS4168_CTRL_PIN, 1); // 假设分压电路已处理
}

// 初始化NS4168的I²S发送
void ns4168_i2s_init(void) {
    // I²S通道配置
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));

    // 标准模式配置（单声道）
    i2s_std_config_t std_cfg_tx = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(NS4168_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(NS4168_BITS_PER_SAMPLE, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .bclk = NS4168_BCLK_PIN,
            .ws = NS4168_LRCLK_PIN,
            .dout = NS4168_SDATA_PIN,
            .din = I2S_GPIO_UNUSED,
            .mclk = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg_tx));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
    ESP_LOGI("NS4168", "NS4168 I2S初始化完成");
}
