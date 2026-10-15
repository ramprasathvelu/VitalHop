VitalHop SX1278 RA-02 standalone LoRa test (ESP-IDF 6.1)
===========================================================

You need TWO ESP32 DevKit V1 boards and TWO SX1278 RA-02 433 MHz modules
to test end-to-end radio communication. Flash TX to one ESP32 and RX to
the other. This is a radio-link test only, not the full VitalHop network.

WIRING (both ESP32s, same pin map)
----------------------------------
RA-02 VCC   -> ESP32 3V3 (radio current peaks matter; use a sound 3.3 V supply)
RA-02 GND   -> ESP32 GND
RA-02 SCK   -> GPIO18
RA-02 MISO  -> GPIO19
RA-02 MOSI  -> GPIO23
RA-02 NSS/CS-> GPIO5
RA-02 RESET -> GPIO14
RA-02 DIO0  -> GPIO26

Leave other DIO pins unconnected for this test. RA-02 logic is 3.3 V; never
feed 5 V to its power or GPIO pins. Keep SPI wires short. Make sure no other
device uses these ESP32 pins.

ANTENNA SAFETY / CONNECTOR
--------------------------
Attach a correctly matched 433 MHz antenna before transmitting. The RA-02
typically uses a tiny IPEX/U.FL RF connector. Your previously selected 433 MHz
antenna was RP-SMA female, so it needs a compatible IPEX/U.FL-to-RP-SMA MALE
50-ohm coax pigtail that mates with both ends. Do not force mismatched
connectors; inspect the exact antenna and pigtail ends. Do not transmit without
an antenna connected, because the RF output stage may be damaged.

RADIO SETTINGS
--------------
433.000 MHz, LoRa bandwidth 125 kHz, spreading factor 7, coding rate 4/5,
explicit header, CRC enabled, sync word 0x12, approximate TX output +14 dBm.
Both radios must use matching settings. Use only frequencies and power levels
permitted by applicable local rules.

BUILD / FLASH RECEIVER FIRST
----------------------------
Open ESP-IDF PowerShell:
  cd "$HOME\VitalHop\tests\lora_rx_test"
  idf.py set-target esp32
  idf.py build
  idf.py -p COM10 flash monitor

Change COM10 to the receiver board's COM port. Leave it listening.

BUILD / FLASH TRANSMITTER
-------------------------
Open a second ESP-IDF PowerShell window:
  cd "$HOME\VitalHop\tests\lora_tx_test"
  idf.py set-target esp32
  idf.py build
  idf.py -p COM11 flash monitor

Change COM11 to the transmitter board's COM port. Do not run both boards on
the same COM port. The transmitter sends a short packet every 2 seconds.

EXPECTED OUTPUT
---------------
TX:
  SX127x RegVersion: 0x12 ...
  LoRa configured: 433.000 MHz ...
  TX OK: VITALHOP_TEST|TX01|SEQ=1|HELLO

RX:
  SX127x RegVersion: 0x12 ...
  LoRa configured: 433.000 MHz ...
  Listening for test packets...
  RX OK (... bytes): VITALHOP_TEST|TX01|SEQ=1|HELLO

If RegVersion is not 0x12, troubleshoot radio power/SPI/CS/reset/module identity.
If version is 0x12 but no RX packets appear, verify antennas, both radios' matching
settings, the wiring, and keep the radios a few metres apart for the first test.

The test uses the ESP-IDF legacy SPI master driver, which is available in ESP-IDF
6.1. If compilation fails, share the first compiler error and do not change IDF.
