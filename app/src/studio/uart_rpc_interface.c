/*
 * Copyright (c) 2022 Libre Solar Technologies GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/net/buf.h>

#include <zephyr/logging/log.h>
#include <zmk/studio/rpc.h>

#include "common.h"

// #include <studio-msgs_decode.h>
// #include <studio-msgs_encode.h>

#include <pb_encode.h>
#include <pb_decode.h>

#include <proto/zmk/studio-msgs.pb.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <string.h>

/* change this to any other UART peripheral if desired */
#define UART_DEVICE_NODE DT_CHOSEN(zmk_studio_rpc_uart)

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

NET_BUF_SIMPLE_DEFINE_STATIC(rx_buf, CONFIG_ZMK_STUDIO_MAX_MSG_SIZE);

/* queue to store up to 10 messages (aligned to 4-byte boundary) */
K_MSGQ_DEFINE(req_msgq, sizeof(zmk_Request), 10, 4);

static enum studio_framing_state rpc_framing_state;

void rpc_cb(struct k_work *work) {
    zmk_Request req;
    while (k_msgq_get(&req_msgq, &req, K_NO_WAIT) >= 0) {
        zmk_Response resp = zmk_rpc_handle_request(&req);

        uint8_t payload[CONFIG_ZMK_STUDIO_MAX_MSG_SIZE];

        pb_ostream_t stream = pb_ostream_from_buffer(payload, ARRAY_SIZE(payload));

        /* Now we are ready to encode the message! */
        bool status = pb_encode(&stream, &zmk_Response_msg, &resp);

        size_t how_much = stream.bytes_written;

        LOG_HEXDUMP_DBG(payload, how_much, "Encoded payload");

        uart_poll_out(uart_dev, FRAMING_SOF);
        for (int i = 0; i < how_much; i++) {
            switch (payload[i]) {
            case FRAMING_SOF:
            case FRAMING_ESC:
            case FRAMING_EOF:
                uart_poll_out(uart_dev, FRAMING_ESC);
                break;
            }
            uart_poll_out(uart_dev, payload[i]);
        }

        uart_poll_out(uart_dev, FRAMING_EOF);
    }
}

K_WORK_DEFINE(rpc_work, rpc_cb);

/*
 * Read characters from UART until line end is detected. Afterwards push the
 * data to the message queue.
 */
static void serial_cb(const struct device *dev, void *user_data) {
    uint8_t c;

    if (!uart_irq_update(uart_dev)) {
        return;
    }

    if (!uart_irq_rx_ready(uart_dev)) {
        return;
    }

    /* read until FIFO empty */
    while (uart_fifo_read(uart_dev, &c, 1) == 1) {
        studio_framing_recv_byte(&rx_buf, &rpc_framing_state, c);

        if (rpc_framing_state == FRAMING_STATE_IDLE && rx_buf.len > 0) {
            zmk_Request req = zmk_Request_init_zero;

            pb_istream_t stream = pb_istream_from_buffer(rx_buf.data, rx_buf.len);

            bool status = pb_decode(&stream, &zmk_Request_msg, &req);

            if (status) {
                k_msgq_put(&req_msgq, &req, K_MSEC(1));
                k_work_submit(&rpc_work);
            }

            // TODO: Just clear the bits that stream read!
            net_buf_simple_reset(&rx_buf);
        }
    }
}

static int uart_rpc_interface_init(void) {
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART device not found!");
        return 0;
    }

    /* configure interrupt and callback to receive data */
    int ret = uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);

    if (ret < 0) {
        if (ret == -ENOTSUP) {
            printk("Interrupt-driven UART API support not enabled\n");
        } else if (ret == -ENOSYS) {
            printk("UART device does not support interrupt-driven API\n");
        } else {
            printk("Error setting UART callback: %d\n", ret);
        }
        return ret;
    }

    uart_irq_rx_enable(uart_dev);

    return 0;
}

SYS_INIT(uart_rpc_interface_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
