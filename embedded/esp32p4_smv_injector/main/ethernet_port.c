// SPDX-License-Identifier: GPL-3.0-or-later

#include "ethernet_port.h"

#include "esp_err.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "esp_eth_mac_esp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_AR_PTP_LAB_TX
#include "ptp_lab_task.hpp"
#endif

static const char *TAG = "ar_smv_eth";
static esp_eth_handle_t s_eth_handle = NULL;

// The owner of this evidence is the ESP-IDF ETH_EVENT stream, NOT GUI
// "connected" status, a configured PHY address, nor a stale PTP runtime.
static EventGroupHandle_t s_eth_events = NULL;
static const EventBits_t kDriverStarted = BIT0;
static const EventBits_t kPhyLinkUp = BIT1;
static const EventBits_t kTxReady = BIT0 | BIT1;
static const TickType_t kLinkWaitTicks = pdMS_TO_TICKS(2500);
static const char *s_ptp_preflight_failure = NULL;

static void ar_esp32p4_eth_event(void *arg, esp_event_base_t base,
                                 int32_t event_id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (s_eth_events == NULL) return;
    switch (event_id) {
    case ETHERNET_EVENT_START:
        xEventGroupSetBits(s_eth_events, kDriverStarted);
        break;
    case ETHERNET_EVENT_CONNECTED:
        xEventGroupSetBits(s_eth_events, kPhyLinkUp);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        xEventGroupClearBits(s_eth_events, kPhyLinkUp);
        break;
    case ETHERNET_EVENT_STOP:
        xEventGroupClearBits(s_eth_events, kTxReady);
        break;
    default:
        break;
    }
}

static EventBits_t ar_eth_bits(void)
{
    return s_eth_events == NULL ? 0 : xEventGroupGetBits(s_eth_events);
}

const char *ar_esp32p4_ptp_preflight_reason(void)
{
    return s_ptp_preflight_failure;
}

void ar_esp32p4_ptp_print_link_status(void)
{
    const EventBits_t bits = ar_eth_bits();
    ESP_LOGI(TAG,
             "PTP ETH driver=%s link=%s tx_ready=%u start_reason=%s",
             (bits & kDriverStarted) ? "STARTED" : "STOPPED",
             (bits & kPhyLinkUp) ? "UP" : "DOWN",
             (bits & kTxReady) == kTxReady ? 1U : 0U,
             s_ptp_preflight_failure == NULL ? "NONE" : s_ptp_preflight_failure);
}


// Bench-validated ESP32-P4 Ethernet-board RMII wiring.
// REF_CLK is supplied by the onboard PHY as a 50 MHz input to ESP32-P4.
enum {
    AR_PHY_ADDRESS = 1,
    AR_PHY_RESET_GPIO = 51,
    AR_MDC_GPIO = 31,
    AR_MDIO_GPIO = 52,
    AR_RMII_REF_CLK_GPIO = 50,
    AR_RMII_TX_EN_GPIO = 49,
    AR_RMII_TXD0_GPIO = 34,
    AR_RMII_TXD1_GPIO = 35,
    AR_RMII_CRS_DV_GPIO = 28,
    AR_RMII_RXD0_GPIO = 29,
    AR_RMII_RXD1_GPIO = 30,
};

#if CONFIG_AR_PTP_LAB_TX
static void ar_esp32p4_restore_normal_emac(void)
{
    if (s_eth_handle == NULL) {
        return;
    }

    bool enable = false;
    const esp_err_t result = esp_eth_ioctl(
        s_eth_handle,
        (esp_eth_io_cmd_t)ETH_MAC_ESP_CMD_PTP_ENABLE,
        &enable);
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "Unable to restore normal EMAC mode after failed PTP start: %s",
                 esp_err_to_name(result));
    } else {
        ESP_LOGI(TAG, "Normal EMAC mode restored after failed PTP start");
    }
}
#endif

esp_eth_handle_t ar_esp32p4_eth_init(void)
{
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.flags |= ETH_MAC_FLAG_PIN_TO_CORE;

    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_config.interface = EMAC_DATA_INTERFACE_RMII;
    emac_config.smi_gpio.mdc_num = AR_MDC_GPIO;
    emac_config.smi_gpio.mdio_num = AR_MDIO_GPIO;
    emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_config.clock_config.rmii.clock_gpio = AR_RMII_REF_CLK_GPIO;
    emac_config.emac_dataif_gpio.rmii.tx_en_num = AR_RMII_TX_EN_GPIO;
    emac_config.emac_dataif_gpio.rmii.txd0_num = AR_RMII_TXD0_GPIO;
    emac_config.emac_dataif_gpio.rmii.txd1_num = AR_RMII_TXD1_GPIO;
    emac_config.emac_dataif_gpio.rmii.crs_dv_num = AR_RMII_CRS_DV_GPIO;
    emac_config.emac_dataif_gpio.rmii.rxd0_num = AR_RMII_RXD0_GPIO;
    emac_config.emac_dataif_gpio.rmii.rxd1_num = AR_RMII_RXD1_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    if (mac == NULL) {
        ESP_LOGE(TAG, "Failed to create ESP32-P4 EMAC");
        return NULL;
    }

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = AR_PHY_ADDRESS;
    phy_config.reset_gpio_num = AR_PHY_RESET_GPIO;

    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
    if (phy == NULL) {
        ESP_LOGE(TAG, "Failed to create onboard RMII PHY");
        mac->del(mac);
        return NULL;
    }

    esp_eth_config_t driver_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t handle = NULL;
    const esp_err_t install_result = esp_eth_driver_install(&driver_config, &handle);
    if (install_result != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet driver install failed: %s", esp_err_to_name(install_result));
        mac->del(mac);
        phy->del(phy);
        return NULL;
    }
    s_eth_events = xEventGroupCreate();
    if (s_eth_events == NULL) {
        ESP_LOGE(TAG, "Cannot allocate Ethernet link evidence event group");
        esp_eth_driver_uninstall(handle);
        return NULL;
    }
    const esp_err_t event_result = esp_event_handler_register(
        ETH_EVENT, ESP_EVENT_ANY_ID, &ar_esp32p4_eth_event, NULL);
    if (event_result != ESP_OK) {
        ESP_LOGE(TAG, "Cannot register Ethernet link evidence: %s",
                 esp_err_to_name(event_result));
        vEventGroupDelete(s_eth_events);
        s_eth_events = NULL;
        esp_eth_driver_uninstall(handle);
        return NULL;
    }
    s_eth_handle = handle;

    ESP_LOGI(TAG,
             "Ethernet configured: PHY addr=%d MDC=%d MDIO=%d REF_CLK=%d",
             AR_PHY_ADDRESS, AR_MDC_GPIO, AR_MDIO_GPIO, AR_RMII_REF_CLK_GPIO);
    return handle;
}

bool ar_esp32p4_ptp_start(void)
{
#if CONFIG_AR_PTP_LAB_TX
    s_ptp_preflight_failure = NULL;
    if (s_eth_handle == NULL || s_eth_events == NULL) {
        s_ptp_preflight_failure = "ETHERNET_DRIVER_UNAVAILABLE";
        ESP_LOGE(TAG, "PTP preflight failed: ETHERNET_DRIVER_UNAVAILABLE");
        return false;
    }

    // Wait for BOTH the driver's START event and actual PHY CONNECTED event.
    // A USB-verified board is NOT Ethernet-link proof. Do not enter PTP
    // timestamp mode or attempt ESP-IDF TX against a non-transmitting MAC.
    const EventBits_t ready = xEventGroupWaitBits(
        s_eth_events, kTxReady, pdFALSE, pdTRUE, kLinkWaitTicks);
    if ((ready & kTxReady) != kTxReady) {
        s_ptp_preflight_failure = (ready & kDriverStarted)
            ? "ETHERNET_LINK_DOWN" : "ETHERNET_DRIVER_NOT_STARTED";
        ESP_LOGE(TAG,
                 "PTP preflight failed: %s; Ethernet driver=%s PHY=%s; "
                 "attach an active Ethernet switch/cable, check LINK LED, retry",
                 s_ptp_preflight_failure,
                 (ready & kDriverStarted) ? "STARTED" : "STOPPED",
                 (ready & kPhyLinkUp) ? "UP" : "DOWN");
        return false;
    }

    // M3.B: Studio may send its ordinary explicit PTP START after boot
    // already verified the SOURCE. This command is idempotent, and must
    // never tear down/reseed an active shared EMAC timebase.
    if (ar_ptp_lab_is_running()) return true;
    if (ar_ptp_source_runtime_active()) {
        s_ptp_preflight_failure = "PTP_RUNTIME_ALREADY_ACTIVE";
        return false; // a worker is starting/stopping; do not roll it back
    }
    // SOURCE start is accepted only after verified Announce + Sync + Follow_Up
    // transmission. A failed start must not leave the shared EMAC in IEEE1588
    // timestamp mode because SMV uses the same MAC immediately afterwards.
    const bool started = ar_ptp_lab_try_start(s_eth_handle);
    if (started) {
        return true;
    }

    ar_ptp_lab_stop();
    // The PTP task owns RX callback/runtime teardown. Give that bounded cleanup
    // time to retire before restoring the MAC timestamp mode. This path executes
    // only after the 1.6 s verified-start timeout/failure, never in the SMV hot path.
    vTaskDelay(pdMS_TO_TICKS(50));
    ar_esp32p4_restore_normal_emac();
    return false;
#else
    return false;
#endif
}
