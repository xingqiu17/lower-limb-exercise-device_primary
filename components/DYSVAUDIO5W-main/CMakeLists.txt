idf_component_register(
    SRCS "src/DYSVAudio5W.cpp" "src/dysv_uart_adapter.cpp"
    INCLUDE_DIRS "." "src" "include"
    REQUIRES driver freertos
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC ARDUINO=10813 ESP32=1)
