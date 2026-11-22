/* Send one closed session at a time, bounded even on a full Atom history. */
#include "session_archive.h"

static esp_err_t session_archive_handler(httpd_req_t *req){
    bool influx=strncmp(req->uri,"/api/export.lp",sizeof("/api/export.lp")-1)==0;
    char after[22]={0};
    bool incremental=false;
    if(!influx && httpd_req_get_url_query_len(req)){
        char query[96];
        if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK)
            return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Archivparameter pruefen.");
        esp_err_t found=httpd_query_key_value(query,"after",after,sizeof(after));
        incremental=found==ESP_OK;
        if(found!=ESP_ERR_NOT_FOUND && (!incremental || !session_cursor_valid(after)))
            return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Sitzungskennung pruefen.");
    }
    session_view_t *view=session_view_open(NULL,0,!incremental);
    if(!view)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Sitzungsarchiv nicht lesbar.");
    char line[512];
    httpd_resp_set_type(req,influx?"text/plain; charset=utf-8":"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t err=ESP_OK;
    if(!influx){
        httpd_resp_set_hdr(req,"Content-Disposition","attachment; filename=TeeNet-Ladesitzungen.json");
        snprintf(line,sizeof(line),"{\"schema\":1,\"device\":\"%s\",\"sessions\":[",mqtt_device_id);
        err=httpd_resp_sendstr_chunk(req,line);
    }
    bool first=true;
    char latest[22]="0_0";
    while(err==ESP_OK){
        const charge_session_t *r;
        err=session_view_next(view,&r);
        if(err!=ESP_OK||!r)break;
        if(r->id==view->reader.active_id||(influx&&!r->start_epoch))continue;
        if(incremental){
            char cursor[22];
            session_cursor(cursor,r);
            if(first)strcpy(latest,cursor);
            if(!strcmp(after,cursor))break;
        }
        int len=session_archive_row(line,sizeof(line),mqtt_device_id,r,influx,first);
        if(len<0||(size_t)len>=sizeof(line)){err=ESP_FAIL;break;}
        err=httpd_resp_send_chunk(req,line,len);
        first=false;
    }
    session_view_close(view);
    if(err==ESP_OK&&!influx){
        if(incremental){
            snprintf(line,sizeof(line),"],\"cursor\":\"%s\"}",latest);
            err=httpd_resp_sendstr_chunk(req,line);
        }else err=httpd_resp_sendstr_chunk(req,"]}");
    }
    if(err==ESP_OK)err=httpd_resp_send_chunk(req,NULL,0);
    return err;
}
