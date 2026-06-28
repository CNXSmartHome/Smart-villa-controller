/* Host-test shim for driver/gpio.h — only the types board.h declares with. */
#ifndef SHIM_DRIVER_GPIO_H
#define SHIM_DRIVER_GPIO_H
typedef int gpio_num_t;
#define GPIO_NUM_NC ((gpio_num_t)-1)
#endif
