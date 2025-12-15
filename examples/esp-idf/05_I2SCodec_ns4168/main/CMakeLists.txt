idf_component_register(SRCS "i2s_ns4168_example.c"
                    PRIV_REQUIRES esp_driver_i2s esp_driver_gpio ns4168
                    EMBED_FILES "canon.pcm")
