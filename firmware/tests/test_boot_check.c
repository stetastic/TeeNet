#include "ems_core.h"
#include "boot_check.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define TAG "test"
#define pdTRUE 1
static void test_log(const char *tag,const char *message,...){(void)tag;(void)message;}
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
static const char *esp_err_to_name(int err){(void)err;return "test";}
static settings_t settings;
static bool boot_config_ok,config_storage_ok,web_started,wallbox_ready,meter_ready,house_bus_ready,contact_gpio_ready,wifi_online,phase_gpio_ready;
static bool boot_rollback_armed,boot_validation_pending,boot_validation_ok,boot_validation_failed,phase_startup;
static uint32_t boot_task_seen[5],fake_now;
static int state_lock,locks,valid_calls,invalid_calls,deleted,confirm_error;
static bool possible,advance_tasks=true;
static int64_t phase_stop_at,phase_move_at,phase_zero_at;
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static int64_t now_ms(void){return fake_now;}
static unsigned ticks(unsigned ms){return ms;}
static int xSemaphoreTake(int lock,unsigned delay){(void)lock;(void)delay;LOCK();return 1;}
static void vTaskDelay(unsigned delay){fake_now+=delay;if(advance_tasks)for(unsigned i=0;i<5;i++)boot_task_seen[i]=fake_now;}
static void vTaskDelete(void *task){(void)task;deleted++;}
static void event_locked(const char *event){(void)event;}
static esp_err_t esp_ota_mark_app_valid_cancel_rollback(void){valid_calls++;return confirm_error;}
static bool esp_ota_check_rollback_is_possible(void){return possible;}
static esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void){invalid_calls++;return -1;}
#include "boot_check.inc"
static void fixture(void){
 settings_defaults(&settings);fake_now=1000;valid_calls=invalid_calls=deleted=locks=confirm_error=0;
 boot_config_ok=config_storage_ok=web_started=wallbox_ready=meter_ready=house_bus_ready=contact_gpio_ready=wifi_online=phase_gpio_ready=true;
 boot_validation_pending=phase_startup=advance_tasks=true;boot_validation_ok=boot_validation_failed=boot_rollback_armed=possible=false;
 phase_stop_at=phase_move_at=phase_zero_at=0;for(unsigned i=0;i<5;i++)boot_task_seen[i]=fake_now;
}
int main(void){
 boot_check_t c={.started=0};assert(boot_check_step(&c,1000,true)==BOOT_CHECK_WAIT);assert(boot_check_step(&c,5900,true)==BOOT_CHECK_WAIT);assert(boot_check_step(&c,6000,true)==BOOT_CHECK_PASS);
 c=(boot_check_t){.started=0};assert(boot_check_step(&c,1000,true)==BOOT_CHECK_WAIT);assert(boot_check_step(&c,5000,false)==BOOT_CHECK_WAIT);assert(boot_check_step(&c,6000,true)==BOOT_CHECK_WAIT);assert(boot_check_step(&c,90000,true)==BOOT_CHECK_FAIL);
 fixture();boot_rollback_armed=true;boot_check_task(NULL);assert(valid_calls==1&&boot_validation_ok&&!boot_validation_pending&&!boot_validation_failed&&deleted==1&&!locks);assert(phase_stop_at==fake_now&&phase_zero_at==0);
 fixture();boot_check_task(NULL);assert(!valid_calls&&boot_validation_ok&&!invalid_calls);
 fixture();strcpy(settings.wifi_ssid,"Home");wifi_online=false;boot_rollback_armed=possible=true;boot_check_task(NULL);assert(invalid_calls==1&&boot_validation_failed&&!valid_calls&&!settings.enabled);
 fixture();boot_config_ok=false;boot_check_task(NULL);assert(boot_validation_failed&&!invalid_calls&&!valid_calls);
 fixture();boot_rollback_armed=possible=true;confirm_error=1;boot_check_task(NULL);assert(valid_calls==1&&invalid_calls==1&&boot_validation_failed);
 fixture();settings.zero_feed_enabled=true;boot_task_seen[3]=0;advance_tasks=false;boot_check_task(NULL);assert(boot_validation_failed&&!valid_calls);
 puts("PASS: actual boot observer: stable startup, WLAN/config/task failures, confirmation failure, one rollback attempt, old bootloader/no fallback and charge-blocked recovery");return 0;
}
