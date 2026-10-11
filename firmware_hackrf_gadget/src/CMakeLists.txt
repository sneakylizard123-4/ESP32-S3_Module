set(TUSB_DIR "${CMAKE_CURRENT_LIST_DIR}/tinyusb")

idf_component_register(
  SRCS
    "main.c"
    "usb_descriptors.c"
    "hackrf_usb.c"
    "${TUSB_DIR}/tusb.c"
    "${TUSB_DIR}/common/tusb_fifo.c"
    "${TUSB_DIR}/device/usbd.c"
    "${TUSB_DIR}/device/usbd_control.c"
    "${TUSB_DIR}/class/vendor/vendor_device.c"
    "${TUSB_DIR}/portable/espressif/esp32sx/dcd_esp32sx.c"
  INCLUDE_DIRS
    "."
    "tinyusb"
    "tinyusb/common"
    "tinyusb/device"
    "tinyusb/class/vendor"
    "tinyusb/portable/espressif/esp32sx"
    "soc_shim"
  REQUIRES
    esp_hw_support
    esp_timer
    xtensa
)
