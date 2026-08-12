// SPDX-License-Identifier: GPL-3.0-or-later

#include "ethernet_port.h"

#include "esp_err.h"
#include "esp_eth_mac_esp.h"
#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_AR_PTP_LAB_TX
#include "ptp_lab_task.hpp"
#endif

static const char *TAG = "ar_smv_eth";
static esp_eth_handle_t s_eth_handle = NULL;

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
    s_eth_handle = handle;

#if CONFIG_AR_PTP_LAB_TX
    // The PTP task starts independently and tolerates link-not-ready TX failures
    // while the normal app flow proceeds to esp_eth_start(). Keeping this hook
    // beside driver installation also allows future PTP-only/analyzer firmware.
    ar_ptp_lab_start(handle);
    if (!ar_ptp_lab_is_running()) {
        ESP_LOGW(TAG, "PTP auto-start was rejected or could not start");
    }
#endif

    ESP_LOGI(TAG,
             "Ethernet configured: PHY addr=%d MDC=%d MDIO=%d REF_CLK=%d",
             AR_PHY_ADDRESS, AR_MDC_GPIO, AR_MDIO_GPIO, AR_RMII_REF_CLK_GPIO);
    return handle;
}

bool ar_esp32p4_ptp_start(void)
{
#if CONFIG_AR_PTP_LAB_TX
    if (s_eth_handle == NULL) {
        return false;
    }
    ar_ptp_lab_start(s_eth_handle);
    return ar_ptp_lab_is_running();
#else
    return false;
#endif
}
