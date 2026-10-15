VitalHop ACK + Retry LoRa test (ESP-IDF 6.1)

IMPORTANT: This is a separate test project. Keep your known-good lora_tx_test and lora_rx_test copies/backups.

Use each folder as an ESP-IDF project. The radio settings match the working test: SX1278 RA-02, 433 MHz, BW125, SF7, CR4/5, CRC on. SPI: SCK18 MISO19 MOSI23 NSS5 RESET14 DIO0 26.

Build RX first, flash RX board, and leave its monitor running:
  cd <path>\lora_rx_test
  idf.py build
  idf.py -p COM_RX flash monitor

Build TX on the other board:
  cd <path>\lora_tx_test
  idf.py build
  idf.py -p COM_TX flash monitor

Expected TX logs: TX DATA id=1 attempt=1/3; ACK OK id=1; DELIVERY SUCCESS id=1.
Expected RX logs: RX DATA id=1; TX ACK: ACK|1.

To test retry: stop/turn off the receiver and observe ACK TIMEOUT followed by attempts 2/3 and 3/3, then DELIVERY FAILED. Restart receiver after test.

A packet ID is acknowledged each time, including duplicate packets, but this starter only remembers the most recent received ID for duplicate reporting; production routing requires a more robust duplicate cache.

Use a proper 433 MHz antenna on each module before transmitting. RA-02 is 3.3 V only. Avoid leaving transmitter sending without an antenna. This is a bench protocol test, not medical-grade reliability validation.
