/* The three mutable dates and meter anchor form one atomic checkpoint. Write
   departing days only from the previous durable copy, before replacing it. */
#define DAILY_CHECKPOINT_KEY "day_checkpoint"

static void energy_day_key(char key[8],uint32_t date){
    snprintf(key,8,"day%02u",history_day_slot(date));
}

static esp_err_t energy_day_write(nvs_handle_t nvs,const energy_day_t *day){
    char key[8];
    energy_day_key(key,day->date);
    return nvs_set_blob(nvs,key,day,sizeof(*day));
}

static esp_err_t energy_days_checkpoint_read(nvs_handle_t nvs,meter_totals_t *out){
    size_t bytes=sizeof(*out);
    esp_err_t err=nvs_get_blob(nvs,DAILY_CHECKPOINT_KEY,out,&bytes);
    if(err!=ESP_OK)return err;
    if(bytes!=sizeof(*out)||out->version!=1||!charge_counter_checkpoint_valid(&out->counter)||
       !energy_store_valid(&out->totals))return ESP_ERR_INVALID_SIZE;
    return ESP_OK;
}

static uint32_t energy_latest_date(const energy_day_t days[EMS_DAYS]){
    uint32_t latest=0;
    for(unsigned i=0;i<EMS_DAYS;i++)if(days[i].date>latest)latest=days[i].date;
    return latest;
}

static const energy_day_t *energy_find_date(const energy_day_t days[EMS_DAYS],uint32_t date){
    for(unsigned i=0;i<EMS_DAYS;i++)if(days[i].date==date)return &days[i];
    return NULL;
}

static esp_err_t energy_days_save(nvs_handle_t nvs,const meter_totals_t *next){
    meter_totals_t previous={0};
    esp_err_t err=energy_days_checkpoint_read(nvs,&previous);
    if(err!=ESP_OK&&err!=ESP_ERR_NVS_NOT_FOUND)return err;
    uint32_t last=energy_latest_date(next->totals.days);
    uint32_t first=history_first_date(last);
    for(unsigned i=0;i<EMS_DAYS;i++){
        const energy_day_t *day=&previous.totals.days[i];
        if(!day->date||day->date<first||energy_find_date(next->totals.days,day->date))continue;
        err=energy_day_write(nvs,day);
        if(err!=ESP_OK)return err;
    }
    return nvs_set_blob(nvs,DAILY_CHECKPOINT_KEY,next,sizeof(*next));
}

typedef struct {nvs_handle_t nvs;esp_err_t error;} daily_migration_t;
static bool energy_days_migrate_row(const energy_day_t *day,void *ctx){
    daily_migration_t *migration=ctx;
    migration->error=energy_day_write(migration->nvs,day);
    return migration->error==ESP_OK;
}

static esp_err_t energy_days_load(nvs_handle_t nvs){
    meter_totals_t checkpoint={0};
    esp_err_t err=energy_days_checkpoint_read(nvs,&checkpoint);
    if(err==ESP_ERR_NVS_NOT_FOUND){
        /* The meter anchor and its energy total must come from the same blob.
           A pre-meter firmware only has the independent "totals" blob. */
        const char *key="meter_totals";
        size_t bytes=0;
        err=nvs_get_blob(nvs,key,NULL,&bytes);
        bool metered=err!=ESP_ERR_NVS_NOT_FOUND;
        if(!metered){key="totals";err=nvs_get_blob(nvs,key,NULL,&bytes);}
        if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;
        if(err!=ESP_OK)return err;
        size_t prefix=metered?offsetof(meter_totals_t,totals):0;
        size_t suffix=metered?sizeof(checkpoint.counter):0;
        if(bytes<=prefix+suffix||bytes>sizeof(meter_totals_30_t))return ESP_ERR_INVALID_SIZE;
        unsigned char *blob=malloc(bytes);
        if(!blob)return ESP_ERR_NO_MEM;
        err=nvs_get_blob(nvs,key,blob,&bytes);
        checkpoint.version=1;
        checkpoint.counter.version=1;
        if(err==ESP_OK&&metered){
            uint32_t version;
            memcpy(&version,blob,sizeof(version));
            memcpy(&checkpoint.counter,blob+bytes-suffix,suffix);
            if(version!=1||!charge_counter_checkpoint_valid(&checkpoint.counter))err=ESP_ERR_INVALID_SIZE;
        }
        if(err==ESP_OK){
            daily_migration_t migration={.nvs=nvs,.error=ESP_OK};
            if(!energy_store_visit(blob+prefix,bytes-prefix-suffix,&checkpoint.totals,
                                   energy_days_migrate_row,&migration))
                err=migration.error!=ESP_OK?migration.error:ESP_ERR_INVALID_SIZE;
        }
        free(blob);
        /* Publish only after all valid old dates are safe. An interruption
           before publication simply repeats this absolute-value migration. */
        if(err==ESP_OK)err=nvs_set_blob(nvs,DAILY_CHECKPOINT_KEY,&checkpoint,sizeof(checkpoint));
        if(err==ESP_OK)err=nvs_commit(nvs);
        /* Leave the old keys as a one-time rollback checkpoint. */
    }
    if(err==ESP_OK){
        energy.store=checkpoint.totals;
        charge_counter.saved=checkpoint.counter;
        charge_counter.restored=true;
    }
    return err;
}

typedef struct {
    nvs_handle_t nvs;
    energy_day_t hot[EMS_DAYS];
    energy_day_t committed[EMS_DAYS];
    uint32_t next,last;
    bool open;
} energy_day_reader_t;

static esp_err_t energy_day_reader_open(energy_day_reader_t *reader,uint32_t today){
    memset(reader,0,sizeof(*reader));
    LOCK();
    memcpy(reader->hot,energy.store.days,sizeof(reader->hot));
    UNLOCK();
    reader->last=today?today:energy_latest_date(reader->hot);
    reader->next=history_first_date(reader->last);
    esp_err_t err=nvs_open_from_partition("stats","energy",NVS_READONLY,&reader->nvs);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_OK; /* First run, still entirely in RAM. */
    if(err!=ESP_OK)return err;
    meter_totals_t previous;
    err=energy_days_checkpoint_read(reader->nvs,&previous);
    if(err==ESP_OK){
        memcpy(reader->committed,previous.totals.days,sizeof(reader->committed));
        uint32_t last=energy_latest_date(reader->committed);
        if(!today&&last>reader->last){reader->last=last;reader->next=history_first_date(last);}
    }
    else if(err!=ESP_ERR_NVS_NOT_FOUND){nvs_close(reader->nvs);return err;}
    reader->open=true;
    return ESP_OK;
}

static esp_err_t energy_day_reader_next(energy_day_reader_t *reader,energy_day_t *out,bool *found){
    *found=false;
    while(reader->next&&reader->next<=reader->last){
        uint32_t date=reader->next;
        reader->next=history_next_date(date);
        const energy_day_t *cached=energy_find_date(reader->hot,date);
        if(!cached)cached=energy_find_date(reader->committed,date);
        if(cached){
            *out=*cached;
            *found=true;
            return ESP_OK;
        }
        if(!reader->open)continue;
        char key[8];
        energy_day_key(key,date);
        size_t bytes=sizeof(*out);
        esp_err_t err=nvs_get_blob(reader->nvs,key,out,&bytes);
        if(err==ESP_ERR_NVS_NOT_FOUND)continue;
        if(err!=ESP_OK)return err;
        if(bytes!=sizeof(*out))return ESP_ERR_INVALID_SIZE;
        if(out->date==date){*found=true;return ESP_OK;}
    }
    return ESP_OK;
}

static void energy_day_reader_close(energy_day_reader_t *reader){
    if(reader->open){nvs_close(reader->nvs);reader->open=false;}
}
