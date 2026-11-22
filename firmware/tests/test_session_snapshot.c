#include "charge_sessions.h"
#include "shell_search_schedule.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int esp_err_t;
enum {ESP_OK=0,ESP_FAIL=-1};
#include "session_reader_fake.h"
typedef struct{uint32_t period,from_date,to_date;bool custom;} session_filter_t;
static bool session_filter_matches(const charge_session_t *r,const session_filter_t *f){
    return f->custom?r->start_date>=f->from_date&&r->start_date<=f->to_date:!f->period||session_matches(r,f->period);
}
#include "session_snapshot.inc"

int main(void){
    flash_count=512;
    for(unsigned i=0;i<flash_count;i++)flash_rows[i]=(charge_session_t){.id=i+1,.grid_wh=2000,
        .start_date=i<128?20261030:20261122};
    flash_rows[400].grid_wh=999;active_id=512;
    session_filter_t filter={.period=202610};
    session_view_t *view=session_view_open(&filter,0,false);
    const charge_session_t *r;
    for(unsigned i=128;i>0;i--){assert(session_view_next(view,&r)==ESP_OK&&r->id==i);}
    assert(session_view_next(view,&r)==ESP_OK&&!r&&view->retained==511&&view->oldest_date==20261030);
    assert(view->reader.active_id==512);
    session_view_close(view);
    view=session_view_open(NULL,20261122,true);
    for(unsigned id=129;id<=512;id++){if(id==401)continue;assert(session_view_next(view,&r)==ESP_OK&&r->id==id);}
    assert(session_view_next(view,&r)==ESP_OK&&!r&&view->retained==511&&view->oldest_date==20261030);
    session_view_close(view);
    view=session_view_open(NULL,0,false);bad_row=0;
    assert(session_view_next(view,&r)==ESP_FAIL&&!r);
    session_view_close(view);
    reject_open=true;assert(!session_view_open(NULL,0,true)&&!readers);
    assert(shell_search_pause(0)==300000&&shell_search_pause(1)==900000&&shell_search_pause(2)==1800000&&shell_search_pause(999)==1800000);
    puts("PASS: 512 flash rows in either order, date and period filtering, active and sub-kWh rules, metadata, errors, discovery backoff.");
}
