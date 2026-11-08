idf_component_register(
    SRCS "main.cpp" "usb_keyboard.cpp"
    INCLUDE_DIRS "../include" "."
    PRIV_REQUIRES
        esp_http_server esp_wifi esp_event esp_netif esp_timer
        esp_system esp_hw_support nvs_flash
        espressif__esp_tinyusb espressif__mdns espressif__cjson
    EMBED_TXTFILES "../web/index.html"
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
