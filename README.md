# matter-esp32-modbus-tcp-adapter

Designed for the ESP32-S3-ETH from Waveshare.

Compile the web application

```
cd html_app
npm run build
```

Then perform a build and flash

```
idf.py build flash monitor
```

Once flashed and connected via an Ethernet cable, you can open http://modbus-adapter.local in your browser