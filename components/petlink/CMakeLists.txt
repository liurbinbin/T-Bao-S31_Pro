idf_component_register(SRCS "src/PetLink.cpp"
                       INCLUDE_DIRS "src"
                       REQUIRES esp_wifi esp_timer esp_hw_support freertos nvs_flash mbedtls)
