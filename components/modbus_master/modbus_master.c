/**
 * @file modbus_master.c
 * @brief Modbus RTU master implementation (see modbus_master.h).
 *
 * All framing uses caller-/stack-provided buffers; nothing is heap-allocated.
 * Each public call builds a PDU, runs an rs485 transaction (with retries), then
 * validates address/function/CRC before decoding.
 */
#include "modbus_master.h"
#include <string.h>

static const char *TAG = "modbus";

#define MB_FC_READ_HOLDING   0x03
#define MB_FC_READ_INPUT     0x04
#define MB_FC_WRITE_SINGLE   0x06
#define MB_FC_WRITE_MULTIPLE 0x10
#define MB_EXC_FLAG          0x80
#define MB_MAX_ADU           256   /* RTU max frame size */

uint16_t mb_crc16(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= buf[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x0001) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;   /* low byte first on the wire */
}

static inline void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static inline uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

/** Append CRC and run one ADU transaction with retry. resp excludes nothing. */
static svc_err_t mb_transact(mb_master_t *m, uint8_t *adu, size_t pdu_len,
                             uint8_t *resp, size_t resp_cap, size_t *resp_len)
{
    uint16_t crc = mb_crc16(adu, pdu_len);
    adu[pdu_len]     = (uint8_t)(crc & 0xFF);   /* low byte first */
    adu[pdu_len + 1] = (uint8_t)(crc >> 8);
    size_t adu_len = pdu_len + 2;

    svc_err_t rc = SVC_ERR_BUS_TIMEOUT;
    for (int attempt = 0; attempt <= m->retries; ++attempt) {
        size_t n = 0;
        rc = rs485_txn(m->bus, adu, adu_len, resp, resp_cap, &n, m->timeout_ms);
        if (rc != SVC_OK) {
            ESP_LOGW(TAG, "txn attempt %d -> 0x%x", attempt, (int)rc);
            continue;
        }
        /* Smallest legal RTU response is an exception: addr+fc+code+crc = 5.
           Anything shorter is a malformed/short frame. */
        if (n < 5) {
            ESP_LOGW(TAG, "short frame (%u bytes)", (unsigned)n);
            rc = SVC_ERR_CRC;
            continue;
        }
        /* RTU CRC is transmitted low byte first. */
        uint16_t rx_crc = (uint16_t)((resp[n - 1] << 8) | resp[n - 2]);
        if (rx_crc != mb_crc16(resp, n - 2)) {
            ESP_LOGW(TAG, "crc mismatch");
            rc = SVC_ERR_CRC;
            continue;
        }
        *resp_len = n;
        return SVC_OK;
    }
    return rc;
}

/** Shared read path for FC 0x03 / 0x04. */
static svc_err_t mb_read_regs(mb_master_t *m, uint8_t fc, uint8_t slave,
                              uint16_t addr, uint16_t count, uint16_t *out)
{
    SVC_CHECK_ARG(m != NULL && out != NULL);
    SVC_CHECK_ARG(slave >= 1 && slave <= 247);
    SVC_CHECK_ARG(count >= 1 && count <= 125);

    uint8_t adu[MB_MAX_ADU];
    adu[0] = slave;
    adu[1] = fc;
    put_u16(&adu[2], addr);
    put_u16(&adu[4], count);

    uint8_t resp[MB_MAX_ADU];
    size_t rlen = 0;
    SVC_RETURN_ON_ERR(mb_transact(m, adu, 6, resp, sizeof(resp), &rlen));

    /* Address must echo. (rlen >= 5 is guaranteed by mb_transact.) */
    if (resp[0] != slave) {
        ESP_LOGW(TAG, "addr mismatch (got %u want %u)", resp[0], slave);
        return SVC_ERR_MODBUS_EXC;
    }
    /* Exception frame: exact length 5 (addr+fc+code+crc). */
    if (resp[1] & MB_EXC_FLAG) {
        if (rlen != 5) {
            return SVC_ERR_CRC;
        }
        ESP_LOGW(TAG, "exception code 0x%02x", resp[2]);
        return SVC_ERR_MODBUS_EXC;
    }
    /* Normal frame: function must match, byte count must equal count*2, and the
       total length must be exactly addr+fc+bc + count*2 + crc = 5 + count*2. */
    if (resp[1] != fc) {
        return SVC_ERR_MODBUS_EXC;
    }
    uint8_t byte_count = resp[2];
    size_t expected = 5u + (size_t)count * 2u;
    if (byte_count != count * 2 || rlen != expected) {
        ESP_LOGW(TAG, "bad read len (bc=%u rlen=%u exp=%u)",
                 byte_count, (unsigned)rlen, (unsigned)expected);
        return SVC_ERR_MODBUS_EXC;
    }
    for (uint16_t i = 0; i < count; ++i) {
        out[i] = get_u16(&resp[3 + i * 2]);
    }
    return SVC_OK;
}

svc_err_t mb_master_init(mb_master_t *m, rs485_handle_t bus,
                         uint32_t timeout_ms, uint8_t retries)
{
    SVC_CHECK_ARG(m != NULL && bus != NULL);
    m->bus = bus;
    m->timeout_ms = timeout_ms ? timeout_ms : 200;
    m->retries = retries;
    return SVC_OK;
}

svc_err_t mb_read_holding(mb_master_t *m, uint8_t slave, uint16_t addr,
                          uint16_t count, uint16_t *out)
{
    return mb_read_regs(m, MB_FC_READ_HOLDING, slave, addr, count, out);
}

svc_err_t mb_read_input(mb_master_t *m, uint8_t slave, uint16_t addr,
                        uint16_t count, uint16_t *out)
{
    return mb_read_regs(m, MB_FC_READ_INPUT, slave, addr, count, out);
}

svc_err_t mb_write_single(mb_master_t *m, uint8_t slave, uint16_t addr,
                          uint16_t value)
{
    SVC_CHECK_ARG(m != NULL);
    SVC_CHECK_ARG(slave >= 1 && slave <= 247);

    uint8_t adu[MB_MAX_ADU];
    adu[0] = slave;
    adu[1] = MB_FC_WRITE_SINGLE;
    put_u16(&adu[2], addr);
    put_u16(&adu[4], value);

    uint8_t resp[MB_MAX_ADU];
    size_t rlen = 0;
    SVC_RETURN_ON_ERR(mb_transact(m, adu, 6, resp, sizeof(resp), &rlen));

    if (resp[0] != slave) {
        return SVC_ERR_MODBUS_EXC;
    }
    if (resp[1] & MB_EXC_FLAG) {
        if (rlen != 5) {
            return SVC_ERR_CRC;
        }
        return SVC_ERR_MODBUS_EXC;
    }
    /* Normal FC06 reply echoes addr+value: addr+fc+reg(2)+val(2)+crc = 8 bytes.
       Verify slave, function, register address AND value all echo exactly. */
    if (rlen != 8 || resp[1] != MB_FC_WRITE_SINGLE ||
        get_u16(&resp[2]) != addr || get_u16(&resp[4]) != value) {
        ESP_LOGW(TAG, "write-single echo mismatch (rlen=%u)", (unsigned)rlen);
        return SVC_ERR_MODBUS_EXC;
    }
    return SVC_OK;
}

svc_err_t mb_write_multiple(mb_master_t *m, uint8_t slave, uint16_t addr,
                            uint16_t count, const uint16_t *values)
{
    SVC_CHECK_ARG(m != NULL && values != NULL);
    SVC_CHECK_ARG(slave >= 1 && slave <= 247);
    SVC_CHECK_ARG(count >= 1 && count <= 123);

    uint8_t adu[MB_MAX_ADU];
    adu[0] = slave;
    adu[1] = MB_FC_WRITE_MULTIPLE;
    put_u16(&adu[2], addr);
    put_u16(&adu[4], count);
    adu[6] = (uint8_t)(count * 2);
    for (uint16_t i = 0; i < count; ++i) {
        put_u16(&adu[7 + i * 2], values[i]);
    }
    size_t pdu_len = 7 + count * 2;

    uint8_t resp[MB_MAX_ADU];
    size_t rlen = 0;
    SVC_RETURN_ON_ERR(mb_transact(m, adu, pdu_len, resp, sizeof(resp), &rlen));

    if (resp[0] != slave) {
        return SVC_ERR_MODBUS_EXC;
    }
    if (resp[1] & MB_EXC_FLAG) {
        if (rlen != 5) {
            return SVC_ERR_CRC;
        }
        return SVC_ERR_MODBUS_EXC;
    }
    /* Normal FC16 reply echoes addr+count: addr+fc+reg(2)+cnt(2)+crc = 8 bytes.
       Verify slave, function, starting address AND register count echo exactly. */
    if (rlen != 8 || resp[1] != MB_FC_WRITE_MULTIPLE ||
        get_u16(&resp[2]) != addr || get_u16(&resp[4]) != count) {
        ESP_LOGW(TAG, "write-multi echo mismatch (rlen=%u)", (unsigned)rlen);
        return SVC_ERR_MODBUS_EXC;
    }
    return SVC_OK;
}
