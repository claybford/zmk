/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <sys/types.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/net/buf.h>

#include <zmk/studio/rpc.h>

#include "uuid.h"
#include "common.h"

#include <pb_encode.h>
#include <pb_decode.h>

#include <proto/zmk/studio-msgs.pb.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

NET_BUF_SIMPLE_DEFINE_STATIC(rx_buf, CONFIG_ZMK_STUDIO_MAX_MSG_SIZE);

static enum studio_framing_state rpc_framing_state;

static void rpc_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    ARG_UNUSED(attr);

    bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);

    LOG_INF("RPC Notifications %s", notif_enabled ? "enabled" : "disabled");
}

static ssize_t read_rpc_resp(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                             uint16_t len, uint16_t offset) {

    LOG_DBG("Read response for length %d at offset %d", len, offset);
    return 0;
}

static void send_response(struct bt_conn *conn, const zmk_Response *response);

static ssize_t write_rpc_req(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
                             uint16_t len, uint16_t offset, uint8_t flags) {
    LOG_HEXDUMP_DBG(buf, len, "Write RPC payload");

    for (int i = 0; i < len; i++) {
        studio_framing_recv_byte(&rx_buf, &rpc_framing_state, ((uint8_t *)buf)[i]);

        if (rpc_framing_state == FRAMING_STATE_IDLE && rx_buf.len > 0) {
            LOG_DBG("Got a full message");
            zmk_Request req = zmk_Request_init_zero;

            pb_istream_t stream = pb_istream_from_buffer(rx_buf.data, rx_buf.len);

            bool status = pb_decode(&stream, &zmk_Request_msg, &req);

            if (status) {
                zmk_Response resp = zmk_rpc_handle_request(&req);

                send_response(conn, &resp);
            }

            net_buf_simple_reset(&rx_buf);
        }
    }

    return len;
}

BT_GATT_SERVICE_DEFINE(
    rpc_interface, BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(ZMK_STUDIO_BT_SERVICE_UUID)),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(ZMK_STUDIO_BT_RPC_CHRC_UUID),
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT, read_rpc_resp,
                           write_rpc_req, NULL),
    BT_GATT_CCC(rpc_ccc_cfg_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT));

static void send_response(struct bt_conn *conn, const zmk_Response *response) {
    uint8_t resp_data[CONFIG_ZMK_STUDIO_MAX_MSG_SIZE];

    pb_ostream_t stream = pb_ostream_from_buffer(resp_data, ARRAY_SIZE(resp_data));

    /* Now we are ready to encode the message! */
    bool status = pb_encode(&stream, &zmk_Response_msg, response);

    if (!status) {
        LOG_ERR("Failed to encode a response message");
        return;
    }

    size_t out_size = stream.bytes_written;

    uint8_t resp_frame[CONFIG_ZMK_STUDIO_MAX_MSG_SIZE * 2];

    int frame_encoded_size =
        studio_framing_encode_frame(resp_data, out_size, resp_frame, sizeof(resp_frame));

    if (frame_encoded_size < 0) {
        LOG_WRN("Failed to encode frame %d", frame_encoded_size);
        return;
    }

    struct bt_gatt_notify_params notify_params = {
        .attr = &rpc_interface.attrs[1],
        .data = &resp_frame,
        .len = frame_encoded_size,
    };

    int err = bt_gatt_notify_cb(conn, &notify_params);
    if (err < 0) {
        LOG_WRN("Failed to notify the response %d", err);
    }
}