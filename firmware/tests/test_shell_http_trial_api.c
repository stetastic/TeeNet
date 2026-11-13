#include "ems_core.h"
#include "evcc_bridge.h"
#include "normal_offset_trial.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
#define ESP_OK 0
#define HTTPD_403_FORBIDDEN 403
#define PHASE_READY 1
typedef int esp_err_t;
typedef struct {int unused;} httpd_req_t;
static settings_t settings;
static evcc_control_t evcc_control;
static evcc_shell_t evcc_shell;
static int64_t clock_ms=1000,actual_at=1000,evcc_shell_at=1000;
static float actual_a[3];
static int phase_state=PHASE_READY,status;
static bool restarting,ota_in_progress,phase_test_active,denied;
static uint32_t evcc_poll_generation;
static manual_report_filter_t manual_filter;
static shell_http_control_t shell_http_trial;
static normal_offset_trial_t normal_offset_trial;
static current_calibration_t current_calibration;
static struct {bool active;} charge_plan;
static struct {bool running;} github_update;
static cJSON *incoming;
static int64_t now_ms(void){return clock_ms;}
static int64_t wallbox_meter_ttl(void){return 15000;}
static bool restart_pending(void){return false;}
static bool authorized(httpd_req_t *r){(void)r;return !denied;}
static cJSON *read_json(httpd_req_t *r){(void)r;cJSON *o=incoming;incoming=NULL;return o;}
static esp_err_t httpd_resp_send_err(httpd_req_t *r,int s,const char *message){(void)r;(void)message;status=s;return 0;}
static esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s){(void)r;status=atoi(s);return 0;}
static esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s){(void)r;(void)s;return 0;}
static esp_err_t ok_json(httpd_req_t *r){(void)r;status=200;return 0;}
#include "shell_http_trial_http.inc"
static int post(const char *json){assert(!incoming);incoming=cJSON_Parse(json);assert(incoming);status=0;httpd_req_t req={0};shell_http_trial_post(&req);cJSON_Delete(incoming);incoming=NULL;return status;}
int main(void){
    settings_defaults(&settings);strcpy(settings.wallbox_meter_type,"shelly_gen2");strcpy(settings.shell_setup_host,"192.168.1.65");
    settings.current_offset_a=.8f;
    evcc_shell.state='B';evcc_shell.http_control=(shell_http_sample_t){.pilot_valid=true,.pilot_a=0};
    const char *arm="{\"pin\":\"4040\",\"action\":\"arm\",\"confirm_risk\":true}";
    assert(post("{\"pin\":\"4040\",\"action\":\"arm\"}")==409&&!shell_http_trial.armed);
    settings.enabled=true;assert(post(arm)==409);settings.enabled=false;
    actual_a[0]=1.01f;assert(post(arm)==409);actual_a[0]=0;
    github_update.running=true;assert(post(arm)==409&&!shell_http_trial.armed);
    assert(post("{\"pin\":\"4040\",\"action\":\"offset_begin\",\"offset_a\":0.7,\"confirm_risk\":true}")==409&&!normal_offset_trial.armed);
    github_update.running=false;
    assert(post("{\"pin\":\"1234\",\"action\":\"arm\",\"confirm_risk\":true}")==403);
    assert(post("{\"pin\":\"4040\",\"action\":\"arm\",\"action\":\"stop\",\"confirm_risk\":true}")==403);
    assert(post("{\"pin\":\"4040\",\"action\":\"arm\",\"confirm_risk\":true,\"interval_s\":6,\"pulse_s\":6}")==403);
    assert(post(arm)==200&&shell_http_trial.armed&&shell_http_trial.lease_until==16000);
    assert(shell_http_trial.normal_offset_a==.8f); /* Capture qualified output neutral compensation on arm. */
    assert(post(arm)==409); /* No reset/re-arm can erase an in-flight trial. */
    clock_ms=2000;assert(post("{\"pin\":\"4040\",\"action\":\"keepalive\"}")==200&&shell_http_trial.lease_until==17000);
    clock_ms=17000;assert(post("{\"pin\":\"4040\",\"action\":\"keepalive\"}")==409);
    settings.enabled=true;settings.mode=MODE_MANUAL;actual_at=17000;actual_a[0]=9;
    assert(post("{\"pin\":\"4040\",\"action\":\"stop\"}")==200&&!settings.enabled&&shell_http_trial.armed);
    actual_a[0]=0;assert(post("{\"pin\":\"4040\",\"action\":\"stop\"}")==200&&!shell_http_trial.armed);
    evcc_shell_at=17000;
    settings.current_offset_a=1.1f;
    assert(post("{\"pin\":\"4040\",\"action\":\"arm\",\"confirm_risk\":true,\"step_a\":0.5,\"bias_a\":0.3,\"interval_s\":8,\"pulse_s\":4}")==200);
    assert(shell_http_trial.step_a==.5f&&fabsf(shell_http_trial.pilot_bias_a-.3f)<.001f&&shell_http_trial.interval_ms==8000&&shell_http_trial.pulse_ms==4000);
    assert(shell_http_trial.normal_offset_a==1.1f);
    settings.current_offset_a=.8f;assert(shell_http_trial.normal_offset_a==1.1f); /* Trial snapshot stays fixed. */
    assert(settings.min_charge_a==EMS_DEFAULT_MIN_CHARGE_A&&!settings.enabled);
    assert(post("{\"pin\":\"4040\",\"action\":\"offset_begin\",\"offset_a\":0.7,\"confirm_risk\":true}")==409);
    assert(post("{\"pin\":\"4040\",\"action\":\"stop\"}")==200&&!shell_http_trial.armed);
    settings.current_offset_a=.8f;
    assert(post("{\"pin\":\"4040\",\"action\":\"offset_begin\",\"offset_a\":0.7}")==409);
    assert(post("{\"pin\":\"4040\",\"action\":\"offset_begin\",\"offset_a\":0.3,\"confirm_risk\":true}")==409);
    assert(post("{\"pin\":\"4040\",\"action\":\"offset_begin\",\"offset_a\":0.7,\"confirm_risk\":true}")==200);
    assert(normal_offset_trial.armed&&fabsf(settings.current_offset_a-.7f)<.0001f);
    assert(post(arm)==409);
    assert(post("{\"pin\":\"4040\",\"action\":\"offset_keepalive\"}")==200);
    settings.enabled=true;actual_a[0]=9;
    assert(post("{\"pin\":\"4040\",\"action\":\"offset_end\"}")==200&&!settings.enabled&&normal_offset_trial.armed&&normal_offset_trial.fault);
    assert(!normal_offset_trial_ready(&normal_offset_trial,clock_ms));
    assert(post("{\"pin\":\"4040\",\"action\":\"offset_keepalive\"}")==409);
    actual_a[0]=0;
    assert(post("{\"pin\":\"4040\",\"action\":\"offset_end\"}")==200&&!normal_offset_trial.armed);
    assert(fabsf(settings.current_offset_a-.8f)<.0001f);
    puts("PASS: protected HTTP/normal-offset trials; bounded reversible offset, mutually exclusive ownership, stop and independent zero restore");
}
