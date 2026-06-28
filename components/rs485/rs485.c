/**
 * @file rs485.c
 * @brief Half-duplex RS485 transport implementation (see rs485.h).
 */
#include "rs485.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "rs485";

#define RS485_RX_BUF   512
#define RS485_TX_BUF   256
/* Idle gap (in symbol times) that marks end-of-frame in RS485 mode. */
#define RS485_RX_TOUT_SYMBOLS 3
/* Extra margin added to the per-transaction timeout when waiting for the bus
   mutex, so a legitimately long in-flight transaction is not falsely rejected. */
#define RS485_LOCK_EXTRA_MS   250

struct rs485_bus {
    uart_port_t      port;
    gpio_num_t       de_gpio;
    SemaphoreHandle_t lock;
    bool             open;
};

/* Two static instances — no post-boot allocation of bus objects. */
static struct rs485_bus s_buses[BOARD_RS485_COUNT];

static void resolve_pins(board_rs485_id_t id, uart_port_t *port,
                         gpio_num_t *tx, gpio_num_t *rx, gpio_num_t *de)
{
    if (id == BOARD_RS485_A) {
        *port = BOARD_RS485A_UART; *tx = BOARD_GPIO_RS485A_TX;
        *rx = BOARD_GPIO_RS485A_RX; *de = BOARD_GPIO_RS485A_DE;
    } else {
        *port = BOARD_RS485B_UART; *tx = BOARD_GPIO_RS485B_TX;
        *rx = BOARD_GPIO_RS485B_RX; *de = BOARD_GPIO_RS485B_DE;
    }
}

static uart_parity_t to_parity(char p)
{
    switch (p) {
    case 'E': case 'e': return UART_PARITY_EVEN;
    case 'O': case 'o': return UART_PARITY_ODD;
    default:            return UART_PARITY_DISABLE;
    }
}

svc_err_t rs485_open(const rs485_cfg_t *cfg, rs485_handle_t *out)
{
    SVC_CHECK_ARG(cfg != NULL && out != NULL);
    SVC_CHECK_ARG(cfg->id < BOARD_RS485_COUNT);

    struct rs485_bus *b = &s_buses[cfg->id];
    if (b->open) {
        *out = b;
        return SVC_OK;
    }

    uart_port_t port; gpio_num_t tx, rx, de;
    resolve_pins(cfg->id, &port, &tx, &rx, &de);

    const uart_config_t uc = {
        .baud_rate = (int)cfg->baud,
        .data_bits = (cfg->data_bits == 7) ? UART_DATA_7_BITS : UART_DATA_8_BITS,
        .parity    = to_parity(cfg->parity),
        .stop_bits = (cfg->stop_bits == 2) ? UART_STOP_BITS_2 : UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* Install first; on ANY later failure, unwind the driver before returning
       so a half-configured UART is never left installed (P7: init cleanup). */
    SVC_RETURN_ON_ERR(uart_driver_install(port, RS485_RX_BUF, RS485_TX_BUF,
                                          0, NULL, 0));

    svc_err_t _err_rc;
    SVC_GOTO_ON_ERR(uart_param_config(port, &uc), fail);
    /* DE/RE is driven by the UART RTS line in hardware RS485 half-duplex mode. */
    SVC_GOTO_ON_ERR(uart_set_pin(port, tx, rx, de, UART_PIN_NO_CHANGE), fail);
    SVC_GOTO_ON_ERR(uart_set_mode(port, UART_MODE_RS485_HALF_DUPLEX), fail);
    SVC_GOTO_ON_ERR(uart_set_rx_timeout(port, RS485_RX_TOUT_SYMBOLS), fail);

    if (b->lock == NULL) {
        b->lock = xSemaphoreCreateMutex();
        if (b->lock == NULL) {
            _err_rc = ESP_ERR_NO_MEM;
            goto fail;
        }
    }

    b->port = port;
    b->de_gpio = de;
    b->open = true;
    *out = b;
    ESP_LOGI(TAG, "bus %d open (uart%d, %lu baud)", cfg->id, port,
             (unsigned long)cfg->baud);
    return SVC_OK;

fail:
    uart_driver_delete(port);   /* unwind the only resource acquired so far */
    ESP_LOGE(TAG, "bus %d open failed (0x%x); driver uninstalled",
             cfg->id, (int)_err_rc);
    return _err_rc;
}

svc_err_t rs485_txn(rs485_handle_t h,
                    const uint8_t *req, size_t req_len,
                    uint8_t *resp, size_t resp_cap, size_t *out_len,
                    uint32_t timeout_ms)
{
    SVC_CHECK_ARG(h != NULL && req != NULL && req_len > 0);
    SVC_CHECK_ARG(out_len != NULL);
    if (!h->open) {
        return ESP_ERR_INVALID_STATE;
    }
    *out_len = 0;

    /* Bounded acquire: never block a control-critical caller forever on the bus
       mutex. If another transaction holds it beyond the budget, fail explicitly. */
    uint32_t lock_ms = timeout_ms + RS485_LOCK_EXTRA_MS;
    if (xSemaphoreTake(h->lock, pdMS_TO_TICKS(lock_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "bus mutex timeout after %ums", (unsigned)lock_ms);
        return SVC_ERR_BUS_TIMEOUT;
    }
    svc_err_t rc = SVC_OK;

    /* Discard any stale bytes before starting a fresh transaction. */
    uart_flush_input(h->port);

    int written = uart_write_bytes(h->port, req, req_len);
    if (written != (int)req_len) {
        rc = ESP_FAIL;
        goto done;
    }
    /* Block until the last bit has left the shifter so DE releases cleanly. */
    if (uart_wait_tx_done(h->port, pdMS_TO_TICKS(timeout_ms)) != ESP_OK) {
        rc = SVC_ERR_BUS_TIMEOUT;
        goto done;
    }

    if (resp != NULL && resp_cap > 0) {
        int n = uart_read_bytes(h->port, resp, resp_cap,
                                pdMS_TO_TICKS(timeout_ms));
        if (n < 0) {
            rc = ESP_FAIL;
            goto done;
        }
        if (n == 0) {
            rc = SVC_ERR_BUS_TIMEOUT;
            goto done;
        }
        *out_len = (size_t)n;
    }

done:
    xSemaphoreGive(h->lock);
    return rc;
}

svc_err_t rs485_close(rs485_handle_t h)
{
    SVC_CHECK_ARG(h != NULL);
    if (!h->open) {
        return SVC_OK;
    }
    svc_err_t rc = uart_driver_delete(h->port);
    h->open = false;
    return rc;
}
