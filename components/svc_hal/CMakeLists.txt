# Common HAL dispatcher + interface. Component is named svc_hal to avoid
# shadowing ESP-IDF's internal hal component/include namespace. Exactly one
# hal_<target> component registers a driver+profile at boot.
idf_component_register(
    SRCS "hal_board.c"
    INCLUDE_DIRS "include"
    REQUIRES svc_common
)
