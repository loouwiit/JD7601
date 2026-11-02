idf_component_register(SRCS "main.cpp"
                    "driver/spi/spi.cpp"
                    "driver/task/task.cpp"
                    "driver/wifi/wifi.cpp"
                    "driver/wifi/socketStream.cpp"
                    "driver/wifi/http.cpp"
                    "driver/touchSensor/touchSensor.cpp"
                    PRIV_REQUIRES esp_driver_spi esp_driver_gpio nvs_flash esp_wifi esp_driver_touch_sens
                    INCLUDE_DIRS ".")
