#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "plant_control.h"
#include "nvs.h"
#include "actuator_pump.h"
#include "saltwater_pump.h"
#include "spray_pump.h"
#include "grow_light.h"
#include "wifi_sta.h"

static const char *names[] = {"pump","fertilizer","spray","grow_light"};
typedef struct { char id[33]; int actuator; bool on; int mode; bool automatic; uint32_t duration; int64_t expires; } command_t;
typedef struct { char id[33]; int actuator; char result[16]; char reason[80]; int64_t expires; } receipt_t;
static QueueHandle_t queue;
static plant_control_ack_fn publish_ack;
static char boot_id[9];
static atomic_bool connected, stop_requested;
static bool outputs[4], pending_stop[4], auto_active[4];
static const char *mode_keys[]={"pump","fertilizer","spray","light"};
static int64_t stop_at[4];
static char active_ids[4][33];
static receipt_t receipts[64];
static bool automatic_modes[4];
static float light_value = NAN;
static int64_t light_sample_at, light_valid_at, low_since, high_since;
static bool sensor_fault = true;

static esp_err_t save_mode(int i, bool enabled) {
    nvs_handle_t handle;
    esp_err_t err=nvs_open("automation",NVS_READWRITE,&handle);
    if(err!=ESP_OK) return err;
    err=nvs_set_u8(handle,mode_keys[i],enabled);
    if(err==ESP_OK) err=nvs_commit(handle);
    nvs_close(handle);
    return err;
}


static portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t now_ms(void) { return esp_timer_get_time()/1000; }

static void emit(const receipt_t *r) {
    cJSON *root=cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root,"command_id",r->id);
    cJSON_AddStringToObject(root,"boot_id",boot_id);
    cJSON_AddStringToObject(root,"actuator",names[r->actuator]);
    cJSON_AddStringToObject(root,"result",r->result);
    cJSON_AddStringToObject(root,"reason",r->reason);
    char *json=cJSON_PrintUnformatted(root);
    if(json) { publish_ack(json); cJSON_free(json); }
    cJSON_Delete(root);
}
static receipt_t *find_receipt(const char *id) {
    for(size_t i=0;i<64;i++) if(!strcmp(receipts[i].id,id)) return &receipts[i];
    return NULL;
}
static void acknowledge(const char *id,int index,const char *result,const char *reason) {
    receipt_t *r=find_receipt(id);
    receipt_t temporary={0};
    if(!r) { r=&temporary; strlcpy(r->id,id,sizeof(r->id)); }
    r->actuator=index; strlcpy(r->result,result,sizeof(r->result)); strlcpy(r->reason,reason,sizeof(r->reason)); emit(r);
}
static esp_err_t output(int index,bool on) {
    uint8_t duty=on ? (index==3 ? 100 : 50) : 0;
    switch(index) {
    case 0:return actuator_pump_set_duty_percent(duty);
    case 1:return saltwater_pump_set_duty_percent(duty);
    case 2:return spray_pump_set_duty_percent(duty);
    default:return grow_light_set_duty_percent(duty);
    }
}
static bool stop(int index,const char *reason) {
    esp_err_t err=output(index,false);
    if(err!=ESP_OK) {
        pending_stop[index]=true;
        if(active_ids[index][0]) acknowledge(active_ids[index],index,"failed","output_stop_failed");
        return false;
    }
    portENTER_CRITICAL(&state_mux); outputs[index]=false; portEXIT_CRITICAL(&state_mux);
    pending_stop[index]=false;
    auto_active[index]=false;
    stop_at[index]=0;
    if(active_ids[index][0]) acknowledge(active_ids[index],index,"done",reason);
    active_ids[index][0]=0;
    return true;
}
static void apply(const command_t *cmd) {
    receipt_t *cached=find_receipt(cmd->id);
    if(cached) {emit(cached);return;}
    int i=cmd->actuator;
    receipt_t *slot=NULL;
    for(size_t n=0;n<64;n++) if(!receipts[n].id[0] || (receipts[n].expires<now_ms() && strcmp(receipts[n].result,"running"))) {slot=&receipts[n];break;}
    if(!slot && (cmd->on || cmd->mode==1)) {acknowledge(cmd->id,i,"rejected","dedupe_capacity");return;}
    if(slot) {memset(slot,0,sizeof(*slot));strlcpy(slot->id,cmd->id,sizeof(slot->id));slot->expires=cmd->expires;}
    if(!atomic_load(&connected) || !wifi_sta_is_connected() || atomic_load(&stop_requested)) {acknowledge(cmd->id,i,"rejected","mqtt_disconnected");return;}
    if(cmd->expires < now_ms() || cmd->expires > now_ms()+20000) {acknowledge(cmd->id,i,"rejected","command_expired");return;}
    if(cmd->mode) {
        if(cmd->automatic || cmd->duration) {acknowledge(cmd->id,i,"rejected","invalid_mode");return;}
        bool enabled=cmd->mode==1;
        if(save_mode(i,enabled)!=ESP_OK) {acknowledge(cmd->id,i,"failed","mode_save_failed");return;}
        portENTER_CRITICAL(&state_mux);automatic_modes[i]=enabled;portEXIT_CRITICAL(&state_mux);
        if(i==3)low_since=high_since=0;
        if((i==3 || (!enabled && auto_active[i])) && !stop(i,"automation_mode_changed")) {acknowledge(cmd->id,i,"failed","output_stop_failed");return;}
        acknowledge(cmd->id,i,"done",enabled?"auto_enabled":"auto_paused");return;
    }
    bool auto_allowed=cmd->on && automatic_modes[i] &&
        ((i==2 && cmd->duration==2000) || (i<2 && cmd->duration>=1 && cmd->duration<=30000));
    if(cmd->automatic && !auto_allowed) {
        acknowledge(cmd->id,i,"rejected","auto_not_allowed");return;
    }
    if(!cmd->automatic) {
        esp_err_t saved=save_mode(i,false);
        portENTER_CRITICAL(&state_mux);automatic_modes[i]=false;portEXIT_CRITICAL(&state_mux);
        if(i==3)low_since=high_since=0;
        if(saved!=ESP_OK) {stop(i,"mode_save_failed");acknowledge(cmd->id,i,"failed","mode_save_failed");return;}
    }
    if(cmd->on && (cmd->duration>600000 || (i!=3 && (cmd->duration==0 || cmd->duration>30000)))) {
        acknowledge(cmd->id,i,"rejected","invalid_duration");return;
    }
    if(cmd->on && i<2 && (outputs[1-i] || pending_stop[1-i] || active_ids[1-i][0])) {
        acknowledge(cmd->id,i,"rejected","pump_interlock");return;
    }
    if(cmd->on && (active_ids[i][0] || pending_stop[i])) {acknowledge(cmd->id,i,"rejected","actuator_busy");return;}
    if(!cmd->on) {
        bool ok=stop(i,"stopped_by_command");
        acknowledge(cmd->id,i,ok?"done":"failed",ok?"output_off":"output_stop_failed");return;
    }
    if(output(i,true)!=ESP_OK) {acknowledge(cmd->id,i,"failed","output_start_failed");return;}
    portENTER_CRITICAL(&state_mux); outputs[i]=true; portEXIT_CRITICAL(&state_mux);
    auto_active[i]=cmd->automatic;
    strlcpy(active_ids[i],cmd->id,sizeof(active_ids[i]));
    stop_at[i]=cmd->duration ? now_ms()+cmd->duration : 0;
    acknowledge(cmd->id,i,"running","output_on");
}
static void automatic_light(void) {
    float lux;int64_t sampled,valid;
    portENTER_CRITICAL(&state_mux);
    lux=light_value;sampled=light_sample_at;valid=light_valid_at;
    portEXIT_CRITICAL(&state_mux);
    int64_t time=now_ms();
    bool fault=!valid || time-valid>=10000;
    portENTER_CRITICAL(&state_mux);sensor_fault=fault;portEXIT_CRITICAL(&state_mux);
    if(!automatic_modes[3]) {low_since=high_since=0;return;}
    if(fault) {low_since=high_since=0;if(outputs[3])stop(3,"light_sensor_fault");return;}
    if(!isfinite(lux) || lux<0 || time-sampled>3000) {low_since=high_since=0;return;}
    if(lux<100) {
        high_since=0;if(!low_since)low_since=time;
        if(!outputs[3] && !pending_stop[3] && time-low_since>=10000 && output(3,true)==ESP_OK) {
            portENTER_CRITICAL(&state_mux);outputs[3]=true;portEXIT_CRITICAL(&state_mux);
        }
    } else if(lux>300) {
        low_since=0;if(!high_since)high_since=time;
        if(outputs[3] && time-high_since>=30000)stop(3,"light_sufficient");
    } else low_since=high_since=0;
}
static void worker(void *arg) {
    (void)arg;
    while(true) {
        if(atomic_exchange(&stop_requested,false) || !atomic_load(&connected) || !wifi_sta_is_connected()) {
            for(int i=0;i<4;i++) if(outputs[i] && !(i==3 && automatic_modes[3])) stop(i,"mqtt_disconnected");
            command_t discarded;
            while(xQueueReceive(queue,&discarded,0)==pdTRUE) acknowledge(discarded.id,discarded.actuator,"rejected","mqtt_disconnected");
        }
        for(int i=0;i<4;i++) if(pending_stop[i])stop(i,"stop_retry");
        automatic_light();
        int64_t time=now_ms();
        for(int i=0;i<4;i++) if(stop_at[i] && time>=stop_at[i]) stop(i,"duration_elapsed");
        command_t command;
        if(xQueueReceive(queue,&command,pdMS_TO_TICKS(20))==pdTRUE) apply(&command);
    }
}
esp_err_t plant_control_init(plant_control_ack_fn callback) {
    if(queue) return ESP_OK;
    publish_ack=callback;
    nvs_handle_t handle;
    if(nvs_open("automation",NVS_READONLY,&handle)==ESP_OK) {
        for(int i=0;i<4;i++) {
            uint8_t value=0;
            if(nvs_get_u8(handle,mode_keys[i],&value)==ESP_OK)automatic_modes[i]=value==1;
        }
        nvs_close(handle);
    }
    snprintf(boot_id,sizeof(boot_id),"%08lx",(unsigned long)esp_random());
    queue=xQueueCreate(16,sizeof(command_t));
    if(!queue) return ESP_ERR_NO_MEM;
    if(xTaskCreate(worker,"plant_control",6144,NULL,6,NULL)!=pdPASS) {vQueueDelete(queue);queue=NULL;return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
void plant_control_connection(bool value) {
    atomic_store(&connected,value);
    if(!value) atomic_store(&stop_requested,true);
}
void plant_control_receive(const char *json,size_t length,bool retained) {
    if(!queue || retained || length>1024) return;
    cJSON *root=cJSON_ParseWithLength(json,length);
    if(!root) return;
    cJSON *id=cJSON_GetObjectItemCaseSensitive(root,"command_id"), *boot=cJSON_GetObjectItemCaseSensitive(root,"boot_id");
    cJSON *act=cJSON_GetObjectItemCaseSensitive(root,"actuator"), *action=cJSON_GetObjectItemCaseSensitive(root,"action");
    cJSON *dur=cJSON_GetObjectItemCaseSensitive(root,"duration_ms"), *expiry=cJSON_GetObjectItemCaseSensitive(root,"deadline_uptime_ms");
    cJSON *source=cJSON_GetObjectItemCaseSensitive(root,"source");
    if(!cJSON_IsString(id)||strlen(id->valuestring)!=32||strspn(id->valuestring,"0123456789abcdef")!=32 ||
       !cJSON_IsString(boot)||strcmp(boot->valuestring,boot_id)||!cJSON_IsString(act)||!cJSON_IsString(action)||
       !cJSON_IsString(source)||(strcmp(source->valuestring,"manual") && strcmp(source->valuestring,"auto"))||!cJSON_IsNumber(dur)||!isfinite(dur->valuedouble)||
       dur->valuedouble<0||dur->valuedouble>600000||floor(dur->valuedouble)!=dur->valuedouble||
       !cJSON_IsNumber(expiry)||!isfinite(expiry->valuedouble)||expiry->valuedouble<0||expiry->valuedouble>9007199254740991.0||
       (strcmp(action->valuestring,"on")&&strcmp(action->valuestring,"off")&&strcmp(action->valuestring,"auto_on")&&strcmp(action->valuestring,"auto_off"))) {cJSON_Delete(root);return;}
    int index=-1; for(int i=0;i<4;i++) if(!strcmp(act->valuestring,names[i])) index=i;
    if(index>=0) {
        command_t command={.actuator=index,.mode=!strcmp(action->valuestring,"auto_on")?1:!strcmp(action->valuestring,"auto_off")?2:0,.automatic=!strcmp(source->valuestring,"auto"),.on=!strcmp(action->valuestring,"on"),.duration=(uint32_t)dur->valuedouble,.expires=(int64_t)expiry->valuedouble};
        strlcpy(command.id,id->valuestring,sizeof(command.id));
        // A full queue drops the command; the backend reports lack of ACK as unknown.
        xQueueSend(queue,&command,0);
    }
    cJSON_Delete(root);
}
void plant_control_status(cJSON *root) {
    bool snapshot[4],modes[4],fault;float lux;
    portENTER_CRITICAL(&state_mux); memcpy(snapshot,outputs,sizeof(snapshot));memcpy(modes,automatic_modes,sizeof(modes));lux=light_value;fault=sensor_fault; portEXIT_CRITICAL(&state_mux);
    cJSON_AddStringToObject(root,"boot_id",boot_id);
    cJSON_AddBoolToObject(root,"control_ready",queue!=NULL);
    cJSON_AddBoolToObject(root,"automation_ready",queue!=NULL);
    cJSON_AddBoolToObject(root,"timed_pumps_ready",queue!=NULL);
    cJSON *automation=cJSON_AddObjectToObject(root,"automation");
    cJSON_AddBoolToObject(automation,"pump",modes[0]);
    cJSON_AddBoolToObject(automation,"fertilizer",modes[1]);
    cJSON_AddBoolToObject(automation,"grow_light",modes[3]);
    cJSON_AddBoolToObject(automation,"spray",modes[2]);
    if(isfinite(lux))cJSON_AddNumberToObject(root,"light_lux",lux);else cJSON_AddNullToObject(root,"light_lux");
    cJSON_AddStringToObject(root,"light_fault",fault?"光照传感器无有效读数":"");
    cJSON *states=cJSON_AddObjectToObject(root,"actuators");
    for(int i=0;i<4;i++) cJSON_AddBoolToObject(states,names[i],snapshot[i]);
}


void plant_control_light_sample(float lux) {
    portENTER_CRITICAL(&state_mux);
    light_value=lux;light_sample_at=now_ms();
    if(isfinite(lux) && lux>=0)light_valid_at=light_sample_at;
    portEXIT_CRITICAL(&state_mux);
}
