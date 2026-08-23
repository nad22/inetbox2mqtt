#pragma once

// -----------------------------------------------------------------------------
// Hardware pin configuration for a generic ESP32 Dev Kit.
// Adjust these if your LIN transceiver / status LEDs are wired differently.
// -----------------------------------------------------------------------------

// LIN bus UART (ESP32 hardware UART2)
#define LIN_UART_NUM      2
#define LIN_UART_RX_PIN   16
#define LIN_UART_TX_PIN   17
#define LIN_UART_BAUD     9600

// Status LEDs (optional, active-low). Set to -1 to disable.
#define PIN_LED_WIFI      2   // on when WiFi/AP is up
#define PIN_LED_MQTT      4   // on when MQTT broker connection is up
#define PIN_LED_LIN       15  // on when CPplus/LIN bus traffic (registration) is detected

// Factory-reset button (hold low ~5s during boot to wipe stored config). Set to -1 to disable.
#define PIN_FACTORY_RESET -1
