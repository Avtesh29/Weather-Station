# ESP32 Weather Station with Local and Remote Data Integration

This project demonstrates an ESP32-based weather station that gathers local sensor data and outdoor weather information, then transmits it to a central server. The HTTP communication aspects of this project build upon the concepts demonstrated in the `http_request` example found within the Espressif ESP-IDF repository ([espressif/esp-idf/examples/protocols/http_request](https://github.com/espressif/esp-idf/tree/master/examples/protocols/http_request)).

---
## Overview

The ESP32 microcontroller serves as the core of the weather station. It performs three main functions:
1.  Fetches current outdoor temperature from an online weather service (`wttr.in`).
2.  Reads local temperature and humidity from its own sensors.
3.  Integrates both local and remote weather data, posting it to a Raspberry Pi (or phone) acting as a server, with the outdoor weather query location configured by the server.

---
## How It Works

### Network Configuration & Connectivity
* The ESP32 is configured with Wi-Fi SSID and password credentials via its `sdkconfig.defaults` file, enabling it to connect to a specified wireless network upon startup.
* The server (Raspberry Pi or other device) must be connected to the **same Wi-Fi network** as the ESP32 to ensure successful communication between the two devices.

### Integrated Weather Reporting
This is the primary operational mode, combining remote and local data:
* **Location Configuration**: The ESP32 first makes an HTTP GET request to the server (e.g., `http://<server_ip_on_same_wifi>/location`) to obtain the desired geographical location for which to fetch outdoor weather.
* **Remote Weather Fetch**: Using the location provided by the server, the ESP32 queries `wttr.in` for the outdoor temperature.
* **Local Sensing**: The ESP32 reads its local temperature and humidity sensors.
* **Data Aggregation & Transmission**: The ESP32 combines the fetched outdoor temperature, its local temperature, and local humidity into a single dataset.
* This aggregated data is then sent via an HTTP POST request to the server on port `1234`.
* **Local Logging**: The ESP32 also logs the location, the temperature from `wttr.in`, and its local temperature to its serial monitor for debugging and verification.

---
## Components & Technologies
* **ESP32c3 Microcontroller**: Main processor for sensor reading, Wi-Fi communication, and HTTP requests. Utilizes ESP-IDF for development.
* **Temperature & Humidity Sensor**: Connected to the ESP32 for local environmental readings.
* **Web Server**:
    * Connected to the same Wi-Fi network as the ESP32.
    * Hosts a simple HTTP server on port `1234` to:
        * Receive POST requests with sensor data from the ESP32.
        * Serve the configured location via a GET request to `/location`.
* **`wttr.in`**: Online weather service used to fetch outdoor temperature data.
* **HTTP**: Protocol used for communication between the ESP32 and the server, and between the ESP32 and `wttr.in`.
* **Wi-Fi**: For network connectivity, configured on the ESP32 via `sdkconfig.defaults`.
* **ESP-IDF `http_request` example**: Foundation for implementing HTTP client functionality on the ESP32.

---
## Setup (Conceptual)

1.  **ESP32**:
    * Configure Wi-Fi credentials (SSID and password) in the `sdkconfig.defaults` file.
    * Programmed with firmware to connect to the configured Wi-Fi network.
    * Code to interface with local sensors.
    * Logic to make HTTP GET and POST requests as described above, referencing principles from the ESP-IDF `http_request` example.
2.  **Server**:
    * Connected to the same Wi-Fi network specified in the ESP32's `sdkconfig.defaults`.
    * Running an HTTP server application capable of:
        * Handling POST requests on port `1234` to receive and display/log data.
        * Handling GET requests to `/location` to provide the target city for `wttr.in`.

This setup allows the ESP32 to function autonomously, reporting comprehensive weather data by combining its immediate environment with broader outdoor conditions, all coordinated over a shared local Wi-Fi network.
