#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_camera.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "app_config.h"
#if __has_include("generated_network_config.h")
#include "generated_network_config.h"
#endif
#if __has_include("local_config.h")
#include "local_config.h"
#endif
#ifndef LOCAL_WIFI_SSID
#warning "未找到 generated_network_config.h: 请先运行根目录 start_dev.bat 选择网络"
#define LOCAL_WIFI_SSID "CHANGE_ME"
#define LOCAL_WIFI_PASSWORD "CHANGE_ME"
#define LOCAL_MQTT_BROKER_URI "mqtt://192.168.1.100:1883"
#endif
#ifndef ACTIVE_NETWORK_PROFILE
#define ACTIVE_NETWORK_PROFILE "unconfigured"
#endif
static const char *TAG="CAM_NODE";
static EventGroupHandle_t events; static esp_mqtt_client_handle_t mqtt; static bool mqtt_ok; static char ip[16];
#define WIFI_OK BIT0
static camera_config_t cam_cfg(void) { return (camera_config_t){.pin_pwdn=32,.pin_reset=-1,.pin_xclk=0,.pin_sccb_sda=26,.pin_sccb_scl=27,.pin_d7=35,.pin_d6=34,.pin_d5=39,.pin_d4=36,.pin_d3=21,.pin_d2=19,.pin_d1=18,.pin_d0=5,.pin_vsync=25,.pin_href=23,.pin_pclk=22,.xclk_freq_hz=20000000,.ledc_timer=LEDC_TIMER_0,.ledc_channel=LEDC_CHANNEL_0,.pixel_format=PIXFORMAT_JPEG,.frame_size=CAMERA_FRAME_SIZE,.jpeg_quality=CAMERA_JPEG_QUALITY,.fb_count=CAMERA_FRAME_BUFFER_COUNT,.fb_location=CAMERA_FB_IN_PSRAM,.grab_mode=CAMERA_GRAB_WHEN_EMPTY}; }
static esp_err_t capture(httpd_req_t *r) { camera_fb_t *f=NULL; for(int i=0;i<2;i++){f=esp_camera_fb_get();if(!f){httpd_resp_send_500(r);return ESP_FAIL;}esp_camera_fb_return(f);} f=esp_camera_fb_get(); if(!f){httpd_resp_send_500(r);return ESP_FAIL;} httpd_resp_set_type(r,"image/jpeg"); esp_err_t e=httpd_resp_send(r,(char*)f->buf,f->len); esp_camera_fb_return(f); return e; }
static void http_start(void){httpd_handle_t s;httpd_config_t c=HTTPD_DEFAULT_CONFIG();c.max_open_sockets=3;ESP_ERROR_CHECK(httpd_start(&s,&c));httpd_uri_t a={.uri="/capture",.method=HTTP_GET,.handler=capture};ESP_ERROR_CHECK(httpd_register_uri_handler(s,&a));}
static void mqtt_event(void*a,esp_event_base_t b,int32_t id,void*d){(void)a;(void)b;(void)d;if(id==MQTT_EVENT_CONNECTED)mqtt_ok=true;if(id==MQTT_EVENT_DISCONNECTED)mqtt_ok=false;}
static void status_task(void*x){(void)x;char t[64],p[300];snprintf(t,sizeof(t),"plant/%s/status",CAMERA_DEVICE_ID);while(true){if(mqtt_ok){wifi_ap_record_t a={0};int r=esp_wifi_sta_get_ap_info(&a)==ESP_OK?a.rssi:0;int n=snprintf(p,sizeof(p),"{\"device_id\":\"%s\",\"online\":true,\"rssi\":%d,\"uptime_s\":%lld,\"fault\":\"\",\"stream_url\":\"\",\"snapshot_url\":\"http://%s/capture\"}",CAMERA_DEVICE_ID,r,esp_timer_get_time()/1000000,ip);esp_mqtt_client_publish(mqtt,t,p,n,0,0);}vTaskDelay(pdMS_TO_TICKS(CAMERA_STATUS_PERIOD_MS));}}
static void wifi_evt(void*a,esp_event_base_t b,int32_t id,void*d){(void)a;(void)b;(void)d;if(id==WIFI_EVENT_STA_START||id==WIFI_EVENT_STA_DISCONNECTED){xEventGroupClearBits(events,WIFI_OK);esp_wifi_connect();}}
static void ip_evt(void*a,esp_event_base_t b,int32_t id,void*d){(void)a;(void)b;if(id==IP_EVENT_STA_GOT_IP){ip_event_got_ip_t*e=d;snprintf(ip,sizeof(ip),IPSTR,IP2STR(&e->ip_info.ip));xEventGroupSetBits(events,WIFI_OK);}}
void app_main(void){ESP_LOGI(TAG,"node=%s target=%s profile=%s ssid=%s",CAMERA_DEVICE_ID,CONFIG_IDF_TARGET,ACTIVE_NETWORK_PROFILE,LOCAL_WIFI_SSID);ESP_ERROR_CHECK(nvs_flash_init());ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());esp_netif_create_default_wifi_sta();ESP_ERROR_CHECK(esp_wifi_init(&(wifi_init_config_t)WIFI_INIT_CONFIG_DEFAULT()));events=xEventGroupCreate();ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_evt,NULL,NULL));ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,ip_evt,NULL,NULL));wifi_config_t w={0};strlcpy((char*)w.sta.ssid,LOCAL_WIFI_SSID,sizeof(w.sta.ssid));strlcpy((char*)w.sta.password,LOCAL_WIFI_PASSWORD,sizeof(w.sta.password));ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&w));ESP_ERROR_CHECK(esp_wifi_start());xEventGroupWaitBits(events,WIFI_OK,pdFALSE,pdTRUE,portMAX_DELAY);camera_config_t c=cam_cfg();ESP_ERROR_CHECK(esp_camera_init(&c));http_start();esp_mqtt_client_config_t q={.broker.address.uri=LOCAL_MQTT_BROKER_URI};mqtt=esp_mqtt_client_init(&q);ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt,ESP_EVENT_ANY_ID,mqtt_event,NULL));ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt));xTaskCreate(status_task,"status",4096,NULL,5,NULL);}

