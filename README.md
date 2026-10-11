# VitalHop

**Multi-Hop LoRa Network for Offline Patient Vitals Monitoring Using Edge AI**

## Project Goal

VitalHop is an experimental offline patient-monitoring prototype for
rural and disaster-affected areas.

The system combines patient vital sensing, local status classification,
and priority-aware LoRa communication to deliver important events to a
Primary Health Centre (PHC).

## Current Development Stage

Stage 0: Patient-node firmware and hardware bring-up.

## Patient Node Hardware

- ESP32-WROOM-32 / ESP32 DevKit V1
- MAX30102 heart-rate and SpO2 sensor
- AD8232 ECG module
- DS18B20 temperature sensor
- SX1278 RA-02 433 MHz LoRa module
- SSD1306 OLED (planned)
- Priority test button

## Implementation Roadmap

1. Verify ESP-IDF project and ESP32 programming.
2. Bring up the DS18B20.
3. Integrate MAX30102.
4. Acquire and inspect the AD8232 ECG signal.
5. Combine sensor readings and validity checks.
6. Implement Normal, Warning and Emergency event priorities.
7. Integrate LoRa communication.
8. Add acknowledgement, retry and event buffering.
9. Integrate the relay nodes and PHC gateway.

## Development Environment

- ESP-IDF 6.1
- Embedded C
- FreeRTOS
- Windows PowerShell

## Disclaimer

This is an engineering prototype, not a clinically certified
medical device. Its experimental status classifications are not
medical diagnoses.
