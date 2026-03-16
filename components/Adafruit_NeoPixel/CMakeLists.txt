idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "." "src"
                       REQUIRES esp_driver_gpio esp_timer freertos esp_driver_rmt)

target_compile_definitions(${COMPONENT_LIB} PUBLIC ARDUINO=10813 ESP32=1)
