idf_component_register(SRCS "main.cpp"
                    "driver/spi/spi.cpp"
                    "driver/task/task.cpp"
                    "driver/wifi/wifi.cpp"
                    "driver/wifi/socketStream.cpp"
                    "driver/wifi/http.cpp"
                    PRIV_REQUIRES esp_driver_spi esp_driver_gpio nvs_flash esp_wifi
                    INCLUDE_DIRS ".")
