typedef struct{uint32_t period,from_date,to_date;bool custom;} session_filter_t;
static bool session_date_parse(const char *text,uint32_t *date){
    if(!text||!date||strlen(text)!=10||text[4]!='-'||text[7]!='-')return false;
    unsigned value=0;for(unsigned i=0;i<10;i++){if(i==4||i==7)continue;if(text[i]<'0'||text[i]>'9')return false;value=value*10+(unsigned)(text[i]-'0');}
    unsigned year=value/10000,month=value/100%100,day=value%100;
    if(year<2024||year>2100||month<1||month>12||day<1||day>31)return false;
    *date=value;return true;
}
static bool session_filter_matches(const charge_session_t *r,const session_filter_t *filter){
    return filter->custom?r->start_date>=filter->from_date&&r->start_date<=filter->to_date:(!filter->period||session_matches(r,filter->period));
}
static bool session_query(httpd_req_t *req,session_filter_t *filter,unsigned *offset,char label[32]) {
    char query[192]={0},text[16]={0},from[16]={0},to[16]={0};*offset=0;memset(filter,0,sizeof(*filter));
    if(httpd_req_get_url_query_len(req)>=sizeof(query))return false;
    if(httpd_req_get_url_query_str(req,query,sizeof(query))==ESP_OK){
        bool has_from=httpd_query_key_value(query,"from",from,sizeof(from))==ESP_OK;
        bool has_to=httpd_query_key_value(query,"to",to,sizeof(to))==ESP_OK;
        if(has_from||has_to){if(!has_from||!has_to||!session_date_parse(from,&filter->from_date)||!session_date_parse(to,&filter->to_date)||filter->from_date>filter->to_date)return false;filter->custom=true;}
        else if(httpd_query_key_value(query,"period",text,sizeof(text))==ESP_OK){if(!session_period_parse(text,&filter->period))return false;}
        if(httpd_query_key_value(query,"offset",text,sizeof(text))==ESP_OK){double v;if(!parse_number(text,0,SESSION_ARCHIVE_CAPACITY,&v)||v!=floor(v))return false;*offset=(unsigned)v;}
    }
    if(filter->custom)snprintf(label,32,"%08lu-bis-%08lu",(unsigned long)filter->from_date,(unsigned long)filter->to_date);
    else if(!filter->period)snprintf(label,32,"Archiv");
    else if(filter->period<10000)snprintf(label,32,"%04lu",(unsigned long)filter->period);
    else snprintf(label,32,"%04lu-%02lu",(unsigned long)filter->period/100,(unsigned long)filter->period%100);
    return true;
}
typedef struct {
    uint32_t date,flags;
    unsigned sessions;
    uint64_t charging_ms;
    double grid_wh,solar_wh,cost_eur,grid_cost_eur,solar_cost_eur;
    bool active;
} session_day_t;
static void session_day_add(session_day_t *day,const charge_session_t *r,bool active){
    day->date=r->start_date;day->sessions++;day->charging_ms+=r->charging_ms;
    day->grid_wh+=r->grid_wh+r->unknown_wh;day->solar_wh+=r->solar_wh;day->cost_eur+=r->cost_eur;
    day->grid_cost_eur+=r->grid_cost_eur;day->solar_cost_eur+=r->solar_cost_eur;
    day->flags|=r->flags;day->active|=active;
}
#include "session_snapshot.inc"

static esp_err_t session_json_start(httpd_req_t *req,const char *prefix){
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr_chunk(req,prefix);
}

static esp_err_t session_day_emit(httpd_req_t *req,const session_day_t *day,bool first){
    char line[512];
    int n=snprintf(line,sizeof(line),"%s{\"date\":%lu,\"sessions\":%u,\"charging_s\":%llu,"
        "\"energy_kwh\":%.15g,\"grid_kwh\":%.15g,\"solar_kwh\":%.15g,\"unknown_kwh\":0,"
        "\"cost_eur\":%.15g,\"grid_cost_eur\":%.15g,\"solar_cost_eur\":%.15g,"
        "\"active\":%s,\"flags\":%lu}",first?"":",",(unsigned long)day->date,day->sessions,
        (unsigned long long)(day->charging_ms/1000),(day->grid_wh+day->solar_wh)/1000,
        day->grid_wh/1000,day->solar_wh/1000,day->cost_eur,day->grid_cost_eur,day->solar_cost_eur,
        day->active?"true":"false",(unsigned long)day->flags);
    return n>=0&&(size_t)n<sizeof(line)?httpd_resp_send_chunk(req,line,n):ESP_FAIL;
}

static esp_err_t sessions_handler(httpd_req_t *req){
    session_filter_t filter;unsigned offset;char label[32];
    if(!session_query(req,&filter,&offset,label))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Zeitraum pruefen.");
    session_view_t *view=session_view_open(&filter,0,false);
    if(!view)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Sitzungsarchiv nicht lesbar.");
    esp_err_t err=session_json_start(req,"{\"days\":[");
    unsigned count=0,shown=0;
    session_day_t day={0},total={0};
    while(err==ESP_OK){
        const charge_session_t *r;
        err=session_view_next(view,&r);
        if(err!=ESP_OK)break;
        if(day.sessions&&(!r||day.date!=r->start_date)){
            if(count++>=offset&&shown<20)err=session_day_emit(req,&day,shown++==0);
            day=(session_day_t){0};
        }
        if(!r||err!=ESP_OK)break;
        session_day_add(&day,r,r->id==view->reader.active_id);
        session_day_add(&total,r,false);
    }
    if(err==ESP_OK){
        char tail[480];
        int n=snprintf(tail,sizeof(tail),"],\"period\":\"%s\",\"count\":%u,\"offset\":%u,\"retained\":%u,"
            "\"grid_kwh\":%.15g,\"solar_kwh\":%.15g,\"unknown_kwh\":0,\"cost_eur\":%.15g,"
            "\"grid_cost_eur\":%.15g,\"solar_cost_eur\":%.15g,\"oldest_date\":%lu}",
            label,count,offset,view->retained,total.grid_wh/1000,total.solar_wh/1000,
            total.cost_eur,total.grid_cost_eur,total.solar_cost_eur,(unsigned long)view->oldest_date);
        err=n>=0&&(size_t)n<sizeof(tail)?httpd_resp_send_chunk(req,tail,n):ESP_FAIL;
    }
    session_view_close(view);
    return err==ESP_OK?httpd_resp_send_chunk(req,NULL,0):err;
}
static esp_err_t session_day_handler(httpd_req_t *req){
    char query[96]={0},date_text[16]={0},offset_text[16]={0};
    if(httpd_req_get_url_query_len(req)>=sizeof(query)||httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK||
       httpd_query_key_value(query,"date",date_text,sizeof(date_text))!=ESP_OK||strlen(date_text)!=8)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Datum als YYYYMMDD angeben.");
    uint32_t date=0;
    for(unsigned i=0;i<8;i++){if(date_text[i]<'0'||date_text[i]>'9')return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungueltiges Datum.");date=date*10+(uint32_t)(date_text[i]-'0');}
    if(date/10000<2024||(date/100)%100<1||(date/100)%100>12||date%100<1||date%100>31)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungueltiges Datum.");
    unsigned offset=0;
    if(httpd_query_key_value(query,"offset",offset_text,sizeof(offset_text))==ESP_OK){
        double value;if(!parse_number(offset_text,0,SESSION_ARCHIVE_CAPACITY,&value)||value!=floor(value))
            return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungueltiger Offset.");
        offset=(unsigned)value;
    }
    session_view_t *view=session_view_open(NULL,date,false);
    if(!view)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Sitzungsarchiv nicht lesbar.");
    esp_err_t err=session_json_start(req,"{\"sessions\":[");
    unsigned count=0,shown=0;
    char line[512];
    while(err==ESP_OK){
        const charge_session_t *r;
        err=session_view_next(view,&r);
        if(err!=ESP_OK||!r)break;
        if(count++<offset||shown>=20)continue;
        double grid=(double)r->grid_wh+r->unknown_wh;
        int n=snprintf(line,sizeof(line),"%s{\"id\":%lu,\"start_epoch\":%lu,\"end_epoch\":%lu,"
            "\"charging_s\":%lu,\"energy_kwh\":%.15g,\"grid_kwh\":%.15g,\"solar_kwh\":%.15g,"
            "\"unknown_kwh\":0,\"cost_eur\":%.15g,\"grid_cost_eur\":%.15g,\"solar_cost_eur\":%.15g,"
            "\"flags\":%lu,\"active\":%s}",shown++?",":"",(unsigned long)r->id,(unsigned long)r->start_epoch,
            (unsigned long)r->end_epoch,(unsigned long)(r->charging_ms/1000),(grid+r->solar_wh)/1000,
            grid/1000,r->solar_wh/1000.0,r->cost_eur,r->grid_cost_eur,r->solar_cost_eur,
            (unsigned long)r->flags,r->id==view->reader.active_id?"true":"false");
        err=n>=0&&(size_t)n<sizeof(line)?httpd_resp_send_chunk(req,line,n):ESP_FAIL;
    }
    if(err==ESP_OK){
        snprintf(line,sizeof(line),"],\"date\":%lu,\"count\":%u,\"offset\":%u}",(unsigned long)date,count,offset);
        err=httpd_resp_sendstr_chunk(req,line);
    }
    session_view_close(view);
    return err==ESP_OK?httpd_resp_send_chunk(req,NULL,0):err;
}
static void session_time(uint32_t stamp,char out[24]){
    out[0]=0;if(!stamp)return;time_t t=stamp;struct tm tm;localtime_r(&t,&tm);strftime(out,24,"%Y-%m-%d %H:%M:%S",&tm);
}
static esp_err_t csv_handler(httpd_req_t *req) {
    session_filter_t filter;unsigned offset;char label[32];
    if(!session_query(req,&filter,&offset,label))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Zeitraum pruefen.");
    session_view_t *view=session_view_open(&filter,0,true);
    if(!view)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Sitzungsarchiv nicht lesbar.");
    char attachment[96];snprintf(attachment,sizeof(attachment),"attachment; filename=TeeNet-Ladesitzungen-%s.csv",label);
    httpd_resp_set_type(req,"text/csv; charset=utf-8");httpd_resp_set_hdr(req,"Content-Disposition",attachment);
    esp_err_t err=httpd_resp_sendstr_chunk(req,"\xEF\xBB\xBF" "Sitzung;Beginn;Letzte_Ladung;Ladezeit_Minuten;Energie_kWh;Netz_kWh;Netz_Kosten_EUR;Netz_Prozent;Solar_Akku_kWh;Solar_Akku_Kosten_EUR;Solar_Akku_Prozent;Kosten_EUR;Status\r\n");
    char line[384],start[24],end[24];double grid=0,solar=0,cost=0,grid_cost=0,solar_cost=0;
    while(err==ESP_OK){
        const charge_session_t *r;
        err=session_view_next(view,&r);
        if(err!=ESP_OK||!r)break;
        double total=(double)r->grid_wh+r->solar_wh+r->unknown_wh;
        double session_grid=r->grid_wh+r->unknown_wh,session_cost=r->cost_eur;
        grid+=session_grid;solar+=r->solar_wh;cost+=session_cost;
        grid_cost+=r->grid_cost_eur;solar_cost+=r->solar_cost_eur;
        session_time(r->start_epoch,start);session_time(r->end_epoch,end);
        const char *status=r->id==view->reader.active_id?"Aktiv":"Abgeschlossen";
        snprintf(line,sizeof(line),"%lu;%s;%s;%.1f;%.6f;%.6f;%.4f;%.2f;%.6f;%.4f;%.2f;%.4f;%s\r\n",(unsigned long)r->id,start,end,r->charging_ms/60000.0,total/1000,session_grid/1000,r->grid_cost_eur,total?session_grid*100/total:0,r->solar_wh/1000,r->solar_cost_eur,total?r->solar_wh*100/total:0,session_cost,status);
        for(char *p=line;*p;p++)if(*p=='.')*p=',';
        err=httpd_resp_sendstr_chunk(req,line);
    }
    if(err==ESP_OK){double total=grid+solar;snprintf(line,sizeof(line),"SUMME;;;;%.6f;%.6f;%.4f;%.2f;%.6f;%.4f;%.2f;%.4f;\r\n",total/1000,grid/1000,grid_cost,total?grid*100/total:0,solar/1000,solar_cost,total?solar*100/total:0,cost);for(char *p=line;*p;p++)if(*p=='.')*p=',';err=httpd_resp_sendstr_chunk(req,line);}
    session_view_close(view);if(err==ESP_OK)err=httpd_resp_send_chunk(req,NULL,0);return err;
}
