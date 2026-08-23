## Electrics
A LIN-UART converter is necessary for communication with the TRUMA CPplus. Since there are now several boards available for purchase that already have the converter integrated (e.g. [WoMoLin lin-interface](https://womolin.de/products/lin-interface/)), building your own is optional.

There is no 12V potential at the RJ12 (LIN connector). Therefore, the supply voltage must be obtained separately from the vehicle's electrical system.

The electrical connection via a TJA1020 LIN transceiver to the ESP32 UART is made according to the circuit diagram shown.

<div align = center>

![grafik](https://user-images.githubusercontent.com/10268240/206511684-806cda73-a47d-4070-86ac-6de7d999c5d6.png)

</div>

Examples for the implementation of the concrete connection can be found under [Connection](https://github.com/mc0110/inetbox2mqtt/issues/20) (upstream project).

This firmware uses **UART2** of the ESP32 (**Tx - GPIO17, Rx - GPIO16**), matching `include/Pins.h`:

<div align = center>

![1](https://user-images.githubusercontent.com/65889763/200187420-7c787a62-4b06-4b8d-a50c-1ccb71626118.png)

</div>

The transceiver's RXD/TXD lines connect directly to the ESP32 UART2 pins. No level shifting is needed (thanks to the internal construction of the TJA1020) - it also works fine at 3.3V logic levels, even when the TJA1020 itself is powered from 12V.

**It is important to connect not only the signal lines but also a common ground between the ESP32, the transceiver board and the vehicle's LIN bus.** A missing ground connection is by far the most common reason for a non-working setup.

If you need to change the pins (e.g. a different ESP32 board revision), edit `LIN_UART_RX_PIN` / `LIN_UART_TX_PIN` in `include/Pins.h` and re-flash.
