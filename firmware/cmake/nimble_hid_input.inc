/*
 * SPDX-FileCopyrightText: 2017-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adapted from ESP-IDF esp_hid/src/nimble_hidd.c. Keep the official report
 * lookup and protocol handling; resource pressure must never assert/reset.
 */
static int yumo_hid_input_error(int rc)
{
    if (rc == BLE_HS_ENOMEM) return ESP_ERR_NO_MEM;
    if (rc == BLE_HS_EBUSY || rc == BLE_HS_EAGAIN) return ESP_ERR_TIMEOUT;
    if (rc == BLE_HS_ENOTCONN) return ESP_ERR_INVALID_STATE;
    return ESP_FAIL;
}

static int nimble_hidd_dev_input_set(void *devp, size_t index, size_t id, uint8_t *data, size_t length)
{
    esp_ble_hidd_dev_t *dev = (esp_ble_hidd_dev_t *)devp;
    if (!dev || s_dev != dev || !dev->connected) return ESP_ERR_INVALID_STATE;
    if (index >= dev->devices_len || (!data && length)) return ESP_ERR_INVALID_ARG;

    struct os_mbuf *om = NULL;
    int rc = ble_att_svr_read_local(dev->devices[index].hid_protocol_handle, &om);
    if (rc != 0) return yumo_hid_input_error(rc);
    if (!om) return ESP_ERR_NO_MEM;
    uint8_t protocol = dev->protocol;
    rc = ble_hs_mbuf_to_flat(om, &protocol, sizeof(protocol), NULL);
    os_mbuf_free_chain(om);
    if (rc != 0) return yumo_hid_input_error(rc);
    dev->protocol = protocol;

    hidd_le_report_item_t *report = find_report(id, ESP_HID_REPORT_TYPE_INPUT, &dev->protocol);
    if (!report) return ESP_ERR_NOT_FOUND;
    om = ble_hs_mbuf_from_flat(data, length);
    if (!om) return ESP_ERR_NO_MEM;

    // NimBLE consumes the mbuf on success AND failure. Never free it again.
    rc = ble_gatts_notify_custom(dev->conn_id, report->handle, om);
    return rc == 0 ? ESP_OK : yumo_hid_input_error(rc);
}
