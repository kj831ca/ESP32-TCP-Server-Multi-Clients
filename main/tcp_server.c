/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>


#define PORT CONFIG_EXAMPLE_PORT
#define MAX_CLIENTS 5

typedef struct
{
    int length;
    char data[128];
}tcp_data_t;

typedef struct
{
    int socket ;
    bool isAvailable;
    char addr_str[128];
    char *taskName ;
    tcp_data_t recv_data;
}tcp_client_t;

static tcp_client_t _client[MAX_CLIENTS];

static const char *TAG = "example";
static const char *CLIENT = "client";
//Client task that will spawn from tcp server task.
void vClientTasks(void *pvParameters)
{
    tcp_client_t *ptClient = (tcp_client_t *)pvParameters;
    tcp_data_t *tDataReceive = &ptClient->recv_data;
    //char rx_buffer[128];
    ESP_LOGI(TAG,"Create task name %s", ptClient->taskName);
    while(1)
    {
        tDataReceive->length = recv(ptClient->socket, tDataReceive->data, sizeof(tDataReceive->data) - 1, 0);
            // Error occured during receiving
            if (tDataReceive->length < 0) {
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
                break;
            }
            // Connection closed
            else if (tDataReceive->length == 0) {
                ESP_LOGI(CLIENT, "Connection closed");
                break;
            }
            // Data received
            else {
                

                tDataReceive->data[tDataReceive->length] = 0; // Null-terminate whatever we received and treat like a string
                ESP_LOGI(CLIENT, "Received %d bytes from %s:", tDataReceive->length, ptClient->addr_str);
                ESP_LOGI(CLIENT, "Client:%s Data:%s", ptClient->taskName, tDataReceive->data);

                
                int err = send(ptClient->socket, tDataReceive->data, tDataReceive->length, 0);
                if (err < 0) {
                    ESP_LOGE(CLIENT, "Error occured during sending: errno %d", errno);
                    break;
                }
                // Adding item to Queue ..
                //xQueueSendToBack(xQueue, &tDataReceive,0);
            }
    }
    shutdown(ptClient->socket, 0);
    close(ptClient->socket);
    ESP_LOGI(CLIENT, "Client Disconnected %s", ptClient->taskName);
    ptClient->isAvailable = 0;
    vTaskDelete(NULL);
}
static int handle_new_client(int sock, struct sockaddr_in6 *family)
{
    uint8_t con_index ;
    
    static char *clientTaskNames[] =
    {
        "TK1",
        "TK2",
        "TK3",
        "TK4",
        "TK5"
    };
    //Loop through the connection if any has been taken.
    for(con_index = 0 ; con_index < MAX_CLIENTS ; con_index++)
    {
        if(_client[con_index].isAvailable == 0)
        {
            //Create the clients tasks to handle incomming message.
            _client[con_index].isAvailable = 1 ;
            _client[con_index].socket = sock;
            if (family->sin6_family == PF_INET) {
                inet_ntoa_r(((struct sockaddr_in *)family)->sin_addr.s_addr, _client[con_index].addr_str, 
                    sizeof(_client[con_index].addr_str) - 1);
            } else if (family->sin6_family == PF_INET6) 
            {
                inet6_ntoa_r(family->sin6_addr, _client[con_index].addr_str, 
                    sizeof(_client[con_index].addr_str) - 1);
            }
            _client[con_index].taskName = clientTaskNames[con_index];
            xTaskCreate(vClientTasks,clientTaskNames[con_index],4096,&_client[con_index],4,NULL);
            ESP_LOGI(TAG, "Client accept %s", clientTaskNames[con_index]);
            return 1;
        }
    }
    //If the code reach here mean no more available connections.
    ESP_LOGE(TAG, "Client reach max limit connections");
    shutdown(sock, 0);
    close(sock);
    return 0;
}
/*
static void do_retransmit(const int sock)
{
    int len;
    char rx_buffer[128];

    do {
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
            ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);

            // send() can return less bytes than supplied length.
            // Walk-around for robust implementation. 
            int to_write = len;
            while (to_write > 0) {
                int written = send(sock, rx_buffer + (len - to_write), to_write, 0);
                if (written < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                }
                to_write -= written;
            }
        }
    } while (len > 0);
}
*/
static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    } else if (addr_family == AF_INET6) {
        bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(PORT);
        ip_protocol = IPPROTO_IPV6;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
#if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
    // Note that by default IPV6 binds to both protocols, it is must be disabled
    // if both protocols used at the same time (used in CI)
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
        uint addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Convert ip address to string
        if (source_addr.sin6_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
        } else if (source_addr.sin6_family == PF_INET6) {
            inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        //do_retransmit(sock);
        handle_new_client(sock,&source_addr);

        //shutdown(sock, 0);
        //close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

#ifdef CONFIG_EXAMPLE_IPV4
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET, 5, NULL);
#endif
#ifdef CONFIG_EXAMPLE_IPV6
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET6, 5, NULL);
#endif
}
