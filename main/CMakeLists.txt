idf_component_register(SRCS "main.cpp"
                    "driver/spi/spi.cpp"
                    PRIV_REQUIRES esp_driver_spi esp_driver_gpio
                    INCLUDE_DIRS ".")
