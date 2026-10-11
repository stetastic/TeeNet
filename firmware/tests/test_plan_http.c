#include "ems_core.h"
#include "ems_features.h"
#include "cJSON.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
typedef struct {const char *body;int status;} httpd_req_t;
#define ESP_OK 0
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
static settings_t settings;
static charge_plan_t charge_plan;
static plan_phase_t plan_phase;
static pv_control_t pv_control;
static energy_t energy;
static charge_guard_t charge_guard;
static struct {bool configured;} evcc_control;
static bool evcc_configuring,reboot_required,expert_reboot_pending,restarting,ota_in_progress,plan_dirty,write_ok=true;
static unsigned plan_revision,restores,saves;
static int locks;
static int64_t utc=1791720000000LL;
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static bool restart_pending(void){return reboot_required||expert_reboot_pending;}
static bool authorized(httpd_req_t *r){(void)r;return true;}
static cJSON *read_json(httpd_req_t *r){return cJSON_Parse(r->body);}
static int64_t now_ms(void){return 1000;}
static float active_min_a(void){return settings.min_charge_a;}
static uint32_t calendar(int64_t *epoch,int64_t *midnight){*epoch=utc;*midnight=0;return 20261011;}
static void restore_control_memory_locked(int64_t now,int64_t epoch,bool force){
    assert(locks==1&&now==1000&&epoch==utc&&force&&!settings.enabled);restores++;
}
static void event_locked(const char *event){assert(locks==1&&event);}
static bool save_plan(void){assert(!locks);saves++;return write_ok;}
static int httpd_resp_set_status(httpd_req_t *r,const char *status){r->status=!strcmp(status,"409 Conflict")?409:500;return 0;}
static int httpd_resp_sendstr(httpd_req_t *r,const char *text){(void)r;assert(text);return 0;}
static int httpd_resp_send_err(httpd_req_t *r,int status,const char *text){r->status=status;assert(text);return 0;}
static int ok_json(httpd_req_t *r){r->status=200;return 0;}
#include "plan_http.inc"
static void fixture(void){
    settings_defaults(&settings);settings.zero_feed_enabled=settings.charge_plan_enabled=settings.control_verified=true;
    memset(&charge_plan,0,sizeof(charge_plan));memset(&pv_control,0,sizeof(pv_control));
    memset(&energy,0,sizeof(energy));memset(&charge_guard,0,sizeof(charge_guard));
    evcc_control.configured=evcc_configuring=reboot_required=expert_reboot_pending=restarting=ota_in_progress=plan_dirty=false;
    plan_phase=PLAN_OFF;plan_revision=restores=saves=0;write_ok=true;
}
int main(void){
    (void)restart_pending;(void)restore_control_memory_locked;
    const char *start="{\"active\":true,\"kwh\":10,\"deadline_epoch\":1791727200}";
    fixture();expert_reboot_pending=true;httpd_req_t req={start,0};plan_handler(&req);
    assert(req.status==400&&!settings.enabled&&!charge_plan.active&&saves==0&&restores==0);
    fixture();req=(httpd_req_t){start,0};plan_handler(&req);
    assert(req.status==200&&settings.enabled&&settings.mode==MODE_PV&&charge_plan.active&&saves==1&&restores==1);
    /* Cancellation remains available while an expert restart is pending. */
    settings.enabled=false;expert_reboot_pending=true;req=(httpd_req_t){"{\"active\":false}",0};plan_handler(&req);
    assert(req.status==200&&!settings.enabled&&!charge_plan.active);
    fixture();write_ok=false;req=(httpd_req_t){start,0};plan_handler(&req);
    assert(req.status==500&&!settings.enabled&&!charge_plan.active);
    fixture();evcc_control.configured=true;req=(httpd_req_t){start,0};plan_handler(&req);
    assert(req.status==409&&saves==0&&restores==0);
    assert(!locks);puts("PASS: actual plan endpoint respects expert restart, restores timers before start, permits cancellation and handles storage failure/evcc ownership");
}
