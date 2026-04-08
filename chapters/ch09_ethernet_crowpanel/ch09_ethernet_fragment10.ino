// These defines MUST appear before #include <ETH.h> in arduino-esp32 v3.x
#define ETH_PHY_TYPE  ETH_PHY_IP101
#define ETH_PHY_ADDR  1          // IP101 MDIO address (check your board schematic)
#define ETH_PHY_MDC   31         // Management Data Clock
#define ETH_PHY_MDIO  52         // Management Data I/O
#define ETH_PHY_POWER 51         // PHY power enable pin
// EMAC_CLK_EXT_IN is the correct clock mode for the ESP32-P4.
// (ETH_CLOCK_GPIO0_IN is the ESP32 classic equivalent - not defined on P4.)
#define ETH_CLK_MODE  EMAC_CLK_EXT_IN

#include <ETH.h>
