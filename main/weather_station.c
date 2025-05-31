/* HTTP GET Example using plain POSIX sockets

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "driver/i2c_master.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "sdkconfig.h"

#define I2C_MASTER_SCL_IO           8                           /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           10                          /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              I2C_NUM_0                   /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ          400000                      /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0                           /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0                           /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000

#define SHTC3_ADDR                  0x70        /*!< Address of the SHTC3 sensor */
#define CMD_WAKEUP                  0x3517
#define CMD_MEASURE_T_FIRST         0x7866

/* Constants that aren't configurable in menuconfig */
#define PHONE_SERVER "10.42.0.162"
#define PHONE_PORT "1234"
#define PHONE_GET_PATH "/location"
#define PHONE_POST_PATH "/"

#define WTTR_SERVER "wttr.in"
#define WTTR_PORT "80"
// #define WTTR_PATH "/%s?m&format=%t"

static const char *TAG = "LAB7_3";

static const char *WTTR_PATH =
    "/%s?m&format=%%t";

static const char *PAYLOAD_TEMPLATE = 
    "Location: %s\nWttr.in Temp: %s\nLocal Temp: %.0f C\nHum: %.0f%%\n";

static const char *POST_TEMPLATE = 
    "POST " PHONE_POST_PATH " HTTP/1.0\r\n"
    "Host: " PHONE_SERVER ":" PHONE_PORT "\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: %d\r\n"
    "\r\n"
    "%s";

static const char *PHONE_REQUEST = "GET " PHONE_GET_PATH " HTTP/1.0\r\n"
    "Host: "PHONE_SERVER":"PHONE_PORT"\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "\r\n";

static const char *WTTR_REQUEST = "GET " "%s" " HTTP/1.0\r\n"
    "Host: "WTTR_SERVER":"WTTR_PORT"\r\n"
    "User-Agent: esp-idf/1.0 esp32 curl\r\n"
    "\r\n";

// Checking CRC helper function
uint8_t checksum(uint8_t* data) {
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < 2; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            }
            else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// read 6 bytes
esp_err_t shrc3_read_data(i2c_master_dev_handle_t dev_handle, uint16_t* temperature, uint16_t* hum) {
    uint8_t data[6] = { 0 };

    esp_err_t err = i2c_master_receive(
        dev_handle,
        data,
        sizeof(data),
        I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS
    );

    // Put data into temperature only if checksum indicates valid
    if (checksum(&data[0]) == data[2]) {
        *temperature = ((uint16_t)data[0] << 8) | data[1];
    }
    else {
        printf("Temp Checksum Failed!\n");
        *temperature = 0;
    }

     // Verify humidity CRC
     if (checksum(&data[3]) == data[5]) {
        *hum = ((uint16_t)data[3] << 8) | data[4];
    } else {
        printf("Humidity Checksum Failed!\n");
        *hum = 0;
    }

    return err;
}

esp_err_t shrc3_send_command(i2c_master_dev_handle_t dev_handle, uint16_t cmd) {
    
    uint8_t data[2] = {(uint8_t)((cmd&0xff00)>>8), (uint8_t)(cmd&0xff)};

    return i2c_master_transmit(
        dev_handle, 
        data, 
        sizeof(data), 
        I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS
    );
}


static void i2c_master_init(i2c_master_bus_handle_t *bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, bus_handle));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHTC3_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(*bus_handle, &dev_config, dev_handle));
}

static void http_weather_task(void *pvParameters)
    {
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    struct addrinfo *wttr_res;
    struct in_addr *wttr_addr;
    int wttr_s, s, r;
    char recv_buf[64];

    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    i2c_master_init(&bus_handle, &dev_handle);
    ESP_LOGI(TAG, "I2C initialized successfully");

    uint16_t temp = 0;
    uint16_t hum = 0;

    vTaskDelay(3000 / portTICK_PERIOD_MS);

    while(1) {
        // Connect to wttr.in
        int wttr_err = getaddrinfo(WTTR_SERVER, WTTR_PORT, &hints, &wttr_res);
        if(wttr_err != 0 || wttr_res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d wttr_res=%p", wttr_err, wttr_res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        wttr_addr = &((struct sockaddr_in *)wttr_res->ai_addr)->sin_addr;
        wttr_s = socket(wttr_res->ai_family, wttr_res->ai_socktype, 0);
        if(wttr_s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(wttr_res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        if(connect(wttr_s, wttr_res->ai_addr, wttr_res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(wttr_s);
            freeaddrinfo(wttr_res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        freeaddrinfo(wttr_res);

        // Connect to Phone
        int err = getaddrinfo(PHONE_SERVER, PHONE_PORT, &hints, &res);
        if(err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        freeaddrinfo(res);

        // GET location from phone
        if (write(s, PHONE_REQUEST, strlen(PHONE_REQUEST)) < 0) {
            ESP_LOGE(TAG, "... socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        // Read HTTP response (location from phone) 
        // Gemini assisted to get only location from HTTP response
        char location_http_response[256];
        // This will be your clean string holding just the city name, e.g., "Santa+Cruz"
        char actual_location_str[64]; 
        
        int total_bytes_received = 0;

        // Clear buffers
        bzero(location_http_response, sizeof(location_http_response));
        bzero(actual_location_str, sizeof(actual_location_str));
        // recv_buf is your existing char[64] used for reading chunks

        // ESP_LOGI(TAG, "Reading HTTP response for /location...");
        // Loop to read the full HTTP response from the server
        do {
            bzero(recv_buf, sizeof(recv_buf)); // Clear the chunk buffer
            r = read(s, recv_buf, sizeof(recv_buf) - 1); // Read a chunk
            if (r > 0) {
                recv_buf[r] = '\0'; // Null-terminate the received chunk
                // Append the current chunk to the full response buffer, if space allows
                if ((total_bytes_received + r) < sizeof(location_http_response)) {
                    strncat(location_http_response, recv_buf, r); // Append the actual number of bytes read
                    total_bytes_received += r;
                } else {
                    ESP_LOGW(TAG, "location_http_response buffer full, response might be truncated.");
                    // To ensure the socket is drained for this request if more data is coming:
                    while (read(s, recv_buf, sizeof(recv_buf) - 1) > 0) { /* discard */ }
                    break; // Stop accumulating
                }
            } else if (r < 0) {
                ESP_LOGE(TAG, "Read error from /location, errno=%d", errno);
                // Depending on errno, you might break or continue (e.g. EAGAIN for non-blocking)
                // For blocking sockets, this is usually a fatal error for this read.
            }
            // if r == 0, server has closed connection, loop will terminate
        } while (r > 0);

        ESP_LOGI(TAG, "Full HTTP response for /location (first %d bytes):\n%s", total_bytes_received, location_http_response);

        // Now, parse the accumulated location_http_response to extract the body
        if (total_bytes_received > 0) {
            char *body_start = strstr(location_http_response, "\r\n\r\n"); // Find end of headers
            if (body_start) {
                body_start += 4; // Move pointer past the "\r\n\r\n"

                // Copy the body (the actual location string) to actual_location_str
                strncpy(actual_location_str, body_start, sizeof(actual_location_str) - 1);
                actual_location_str[sizeof(actual_location_str) - 1] = '\0'; // Ensure null-termination

                // Optional: Remove trailing newline characters (e.g. \r or \n)
                // The Python server's wfile.write() doesn't add them, but good practice.
                char *newline_char = strpbrk(actual_location_str, "\r\n");
                if (newline_char != NULL) {
                    *newline_char = '\0'; // Truncate at the first newline character found
                }
                // ESP_LOGI(TAG, "Parsed Location: [%s]", actual_location_str);
                // 'actual_location_str' NOW CONTAINS ONLY THE LOCATION, e.g., "Santa+Cruz"
            } else {
                ESP_LOGE(TAG, "Could not find end of HTTP headers in /location response.");
            }
        } else {
            ESP_LOGE(TAG, "No data received in /location response.");
        }
        // ESP_LOGI(TAG, "Location: %s", actual_location_str);
        close(s);

        // Add location to wttr request
        char wttr_req[512];
        char wttr_path[128];

        int wttr_path_len = snprintf(wttr_path, sizeof(wttr_path), WTTR_PATH,
                            actual_location_str);

        int wttr_req_len = snprintf(wttr_req, sizeof(wttr_req), WTTR_REQUEST,
                            wttr_path);

        // ESP_LOGI(TAG, "Request:\n%s", wttr_req);

        // GET weather info from wttr.in using phone location
        if (write(wttr_s, wttr_req, wttr_req_len) < 0) {
            ESP_LOGE(TAG, "... socket send failed");
            close(wttr_s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        // Read HTTP response (weather info)
        // Gemini assisted to get only temp from HTTP response
        char wttr_http_response[256];
        // This will be your clean C string holding just the temperature, e.g., "+15"
        char actual_wttr_temp_str[16]; // Temperature strings are short (e.g., "-10", "+25")
        
        int wttr_total_bytes_received = 0;

        // Clear buffers
        bzero(wttr_http_response, sizeof(wttr_http_response));
        bzero(actual_wttr_temp_str, sizeof(actual_wttr_temp_str));
        // 'recv_buf' is your existing char[64] used for reading chunks

        // ESP_LOGI(TAG, "Reading HTTP response from wttr.in...");
        // Loop to read the full HTTP response from the server
        do {
            bzero(recv_buf, sizeof(recv_buf)); // Clear the chunk buffer
            r = read(wttr_s, recv_buf, sizeof(recv_buf) - 1); // Read a chunk
            if (r > 0) {
                recv_buf[r] = '\0'; // Null-terminate the received chunk
                // Append the current chunk to the full response buffer, if space allows
                if ((wttr_total_bytes_received + r) < sizeof(wttr_http_response)) {
                    // Use memcpy for potentially non-string data, or strncat if recv_buf is always a string fragment
                    memcpy(wttr_http_response + wttr_total_bytes_received, recv_buf, r);
                    wttr_total_bytes_received += r;
                    wttr_http_response[wttr_total_bytes_received] = '\0'; // Keep it null-terminated
                } else {
                    ESP_LOGW(TAG, "wttr_http_response buffer full, response might be truncated.");
                    // To ensure the socket is drained for this request if more data is coming:
                    while (read(wttr_s, recv_buf, sizeof(recv_buf) - 1) > 0) { /* discard */ }
                    break; // Stop accumulating
                }
            } else if (r < 0) {
                ESP_LOGE(TAG, "Read error from wttr.in, errno=%d", errno);
                // Depending on errno, you might break or continue.
                // For blocking sockets, this is usually a fatal error for this read attempt.
            }
            // if r == 0, server has closed connection, loop will terminate
        } while (r > 0);

        ESP_LOGI(TAG, "Full HTTP response from wttr.in (total %d bytes):\n%s", wttr_total_bytes_received, wttr_http_response);

        // Now, parse the accumulated wttr_http_response to extract the body
        if (wttr_total_bytes_received > 0) {
            char *body_start_wttr = strstr(wttr_http_response, "\r\n\r\n"); // Find end of headers
            if (body_start_wttr) {
                body_start_wttr += 4; // Move pointer past the "\r\n\r\n"
                
                // Copy the body (the actual temperature string) to actual_wttr_temp_str
                strncpy(actual_wttr_temp_str, body_start_wttr, sizeof(actual_wttr_temp_str) - 1);
                actual_wttr_temp_str[sizeof(actual_wttr_temp_str) - 1] = '\0'; // Ensure null-termination

                // wttr.in with format=%t often includes a newline at the end of the temperature
                char *newline_char_wttr = strpbrk(actual_wttr_temp_str, "\r\n");
                if (newline_char_wttr != NULL) {
                    *newline_char_wttr = '\0'; // Truncate at the first newline character found
                }
                // ESP_LOGI(TAG, "Parsed Wttr Temp: [%s]", actual_wttr_temp_str);
                // 'actual_wttr_temp_str' NOW CONTAINS ONLY THE TEMPERATURE STRING
            } else {
                ESP_LOGE(TAG, "Could not find end of HTTP headers in wttr.in response.");
            }
        } else {
            ESP_LOGE(TAG, "No data received in wttr.in response or read error occurred.");
        }
        // ESP_LOGI(TAG, "Wttr Temp: %s", actual_wttr_temp_str);


        // Re-Connect to Phone
        err = getaddrinfo(PHONE_SERVER, PHONE_PORT, &hints, &res);
        if(err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        freeaddrinfo(res);

        // Read temp and hum
        shrc3_send_command(dev_handle, CMD_WAKEUP);
        vTaskDelay(pdMS_TO_TICKS(10)); 

        shrc3_send_command(dev_handle, CMD_MEASURE_T_FIRST); 
        vTaskDelay(pdMS_TO_TICKS(20));
 
        shrc3_read_data(dev_handle, &temp, &hum);
        float temp_c = -45.0f + (175.0f * ((float)(temp) / 65536.0f));
        float hum_p = 100.0f * ((float)(hum) / 65536.0f); 

        // ESP_LOGI(TAG, "Temp: %.0f C, Hum: %.0f %%", temp_c, hum_p);
        // End temp and hum


        // Sending POST Request
        char post_request[512];
        char payload[128];

        int payload_len = snprintf(payload, sizeof(payload), PAYLOAD_TEMPLATE,
                            actual_location_str, actual_wttr_temp_str, temp_c, hum_p);

        int post_request_len = snprintf(post_request, sizeof(post_request), POST_TEMPLATE,
                            payload_len, payload);

        if (write(s, post_request, post_request_len) < 0) {
            ESP_LOGE(TAG, "... socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "Full HTTP reponse from POST:");
        do {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            for(int i = 0; i < r; i++) {
                putchar(recv_buf[i]);
            }
        } while(r > 0);
        printf("\n");
        // ESP_LOGI(TAG, "... socket send success");

        ESP_LOGI(TAG, "Sent to phone:\n%s", payload);
        printf("\n\n");

        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                sizeof(receiving_timeout)) < 0) {
            ESP_LOGE(TAG, "... failed to set socket receiving timeout");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        if (setsockopt(wttr_s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                sizeof(receiving_timeout)) < 0) {
            ESP_LOGE(TAG, "... failed to set socket receiving timeout");
            close(wttr_s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        // ESP_LOGI(TAG, "... set socket receiving timeout success");

        close(s);
        close(wttr_s);
        
        vTaskDelay(5000 / portTICK_PERIOD_MS);

        // ESP_LOGI(TAG, "Starting again!");
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    xTaskCreate(&http_weather_task, "http_weather_task", 8192, NULL, 5, NULL);
    // xTaskCreate(&http_post_task, "http_post_task", 4096, NULL, 5, NULL);
}
