# Modbus -> Matter Bridge

Designed for the ESP32-S3-ETH from Waveshare, this project will create Matter devices for any connected Modbus devices. It will act as a bridge between the two ecosystems, allowing you to easily connect your devices to your Matter ecosystem.

> [!NOTE]
> This project only supports the Solax X1-G4 inverter at this time. I have plans to expand support.

## Building and Flashing

From your esp-idf environment, compile and flash the fireware.

```
idf.py build flash monitor
```

Once flashed and connected with an Ethernet cable, you can open http://modbus-adapter.local in your browser.

## Setup

One the web page opens, visit teh `Modbus Devices` tab.

<img width="1615" height="432" alt="image" src="https://github.com/user-attachments/assets/4726f78b-983f-4703-851e-3d9de667fe80" />

Click `Add Device` and enter the IP address of your Solax Inverter.

<img width="1230" height="788" alt="image" src="https://github.com/user-attachments/assets/4e768a17-aa06-4c30-a67d-0cd6698d485c" />

You should then see your device listed

<img width="1987" height="436" alt="image" src="https://github.com/user-attachments/assets/3cd053e7-fda0-46c2-baad-01f9b26fa6b3" />

To add this device to your Matter fabric, open the `Matter` tab.

<img width="1197" height="319" alt="image" src="https://github.com/user-attachments/assets/578abe0e-58cb-4a13-ad16-bcd6553d46bd" />

Click `Open Commissioning Window` and you'll see the Matter QR code and pairing code.

<img width="1386" height="689" alt="image" src="https://github.com/user-attachments/assets/faba6d7c-25d3-4b51-9b2c-8b59b560f5f5" />

Commission your device as you would any other Matter device.

> [!NOTE]
> Support for Solar Power devices are almost non-existant in the Matter ecosystem. Home Assistant does a good job.

Home Assistant will show the inverter as a Connected Device, since this Modbus Adapter acts as a Bridge.

<img width="590" height="1278" alt="20260429_081045000_iOS" src="https://github.com/user-attachments/assets/a800c00c-36d7-4a7f-9ffa-92ba13b997f6" />

The Solax Inverter will then show the available data

<img width="590" height="1278" alt="20260429_081049000_iOS" src="https://github.com/user-attachments/assets/7c3ea5ac-ec25-40ba-9d89-cf6a0bb457d2" />

> [!NOTE]
> Support is limited to just voltage, power and current at present.

