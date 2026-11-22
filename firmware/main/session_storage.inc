/* The archive is a flash ring. Publish changed 16-row blocks through a single
   bank map, so loss of power before the header write leaves the old ring intact. */
#include "session_checkpoint.h"
#define SESSION_BLOCK_ROWS 16
#define SESSION_BLOCKS (SESSION_ARCHIVE_CAPACITY / SESSION_BLOCK_ROWS)
#define SESSION_SHORT_CAPACITY 50
_Static_assert(SESSION_BLOCKS==32,"The bank map has one bit per block");
typedef struct {uint32_t version,count,next_slot,next_id,active_slot,valid,banks;} session_disk_head_t;
typedef struct {charge_session_t rows[SESSION_BLOCK_ROWS];unsigned loaded;} session_block_t;
static session_disk_head_t session_disk={.version=1,.next_id=1};
static uint32_t session_disk_revision,session_disk_gap_id;

static void session_block_key(char out[12],unsigned block,bool bank){snprintf(out,12,"s%02u%c",block,bank?'b':'a');}
/* A power cut can leave a staged bank or the previous bank. The verified
   published header is the sole owner of live blocks after boot. */
static void session_collect_orphans(nvs_handle_t h,const session_disk_head_t *head){
    for(unsigned b=0;b<SESSION_BLOCKS;b++){
        uint32_t bit=UINT32_C(1)<<b;
        for(unsigned bank=0;bank<2;bank++){
            if((head->valid&bit)&&((head->banks&bit)!=0)==(bank!=0))continue;
            char key[12];session_block_key(key,b,bank!=0);nvs_erase_key(h,key);
        }
    }
}
static bool session_disk_read(nvs_handle_t h,const session_disk_head_t *head,unsigned slot,session_block_t *block){
    unsigned b=slot/SESSION_BLOCK_ROWS;
    if(b>=SESSION_BLOCKS)return false;
    if(block->loaded==b)return true;
    uint32_t bit=UINT32_C(1)<<b;
    if(!(head->valid&bit))return false;
    char key[12];session_block_key(key,b,(head->banks&bit)!=0);
    size_t len=sizeof(block->rows);
    if(nvs_get_blob(h,key,block->rows,&len)!=ESP_OK||len!=sizeof(block->rows))return false;
    block->loaded=b;return true;
}
static esp_err_t session_stage_block(nvs_handle_t h,session_disk_head_t *next,const session_disk_head_t *prior,
                                     unsigned b,const charge_session_t *rows){
    uint32_t bit=UINT32_C(1)<<b;
    bool bank=(prior->valid&bit)&&!(prior->banks&bit);
    char key[12];session_block_key(key,b,bank);
    esp_err_t err=nvs_set_blob(h,key,rows,SESSION_BLOCK_ROWS*sizeof(*rows));
    if(err==ESP_OK){
        next->valid|=bit;
        if(bank)next->banks|=bit;else next->banks&=~bit;
    }
    return err;
}
static esp_err_t session_publish(nvs_handle_t h,const session_disk_head_t *prior,const session_disk_head_t *next){
    esp_err_t err=nvs_set_blob(h,"sess_head",next,sizeof(*next));
    if(err==ESP_OK)err=nvs_commit(h);
    if(err!=ESP_OK)return err;
    session_disk=*next;session_disk_revision++;
    uint32_t obsolete=prior->valid&(~next->valid|(prior->banks^next->banks));
    for(unsigned b=0;b<SESSION_BLOCKS;b++){
        uint32_t bit=UINT32_C(1)<<b;
        if(obsolete&bit){char key[12];session_block_key(key,b,(prior->banks&bit)!=0);nvs_erase_key(h,key);}
    }
    return ESP_OK;
}
static unsigned session_slot_before(unsigned next,unsigned distance){
    return (next+SESSION_ARCHIVE_CAPACITY-distance)%SESSION_ARCHIVE_CAPACITY;
}
/* Session IDs skip zero on overflow. Up to four new IDs may be waiting for a
   checkpoint; older archive rows may contain gaps from an earlier firmware. */
static bool session_appended_count(const session_disk_head_t *head,uint32_t next_id,unsigned count,unsigned *added,bool *gap){
    if(!next_id||!head->next_id)return false;
    uint32_t n=next_id>=head->next_id?next_id-head->next_id:UINT32_MAX-head->next_id+next_id;
    *gap=n>count;
    *added=*gap?count:n;return true;
}
static esp_err_t session_chunks_save_checkpoint(nvs_handle_t h,const session_checkpoint_t *copy){
    const session_disk_head_t old=session_disk;
    if(!copy->count){
        if(copy->next_id!=1)return ESP_ERR_INVALID_STATE;
        if(!old.count)return ESP_OK;
        session_disk_head_t empty={.version=1,.next_id=1};
        esp_err_t err=session_publish(h,&old,&empty);
        if(err==ESP_OK)session_disk_gap_id=0;
        return err;
    }
    unsigned added=0;bool gap;
    if(!session_appended_count(&old,copy->next_id,copy->count,&added,&gap))return ESP_ERR_INVALID_STATE;
    session_disk_head_t next=old;next.version=1;
    next.next_slot=(old.next_slot+added)%SESSION_ARCHIVE_CAPACITY;
    next.next_id=copy->next_id;
    next.count=old.count+added>SESSION_ARCHIVE_CAPACITY?SESSION_ARCHIVE_CAPACITY:old.count+added;
    if(copy->count>next.count)return ESP_ERR_INVALID_STATE;
    next.active_slot=0;
    session_block_t block={.loaded=SESSION_BLOCKS};
    unsigned i=0;
    while(i<copy->count){
        unsigned slot=session_slot_before(next.next_slot,copy->count-i);
        unsigned b=slot/SESSION_BLOCK_ROWS;
        if(old.valid&(UINT32_C(1)<<b)){
            if(!session_disk_read(h,&old,slot,&block))return ESP_ERR_INVALID_STATE;
        }else memset(block.rows,0,sizeof(block.rows));
        bool dirty=false;
        do{
            const charge_session_t *row=&copy->records[i];
            if(!row->id)return ESP_ERR_INVALID_STATE;
            unsigned ram_slot=(copy->next_slot+CHARGE_SESSION_CAPACITY-copy->count+i)%CHARGE_SESSION_CAPACITY;
            if(copy->active_slot==ram_slot+1)next.active_slot=slot+1;
            charge_session_t *stored=&block.rows[slot%SESSION_BLOCK_ROWS];
            if(i<copy->count-added&&stored->id!=row->id)return ESP_ERR_INVALID_STATE;
            charge_session_t value=*row;
            if((gap&&i==0)||(stored->id==row->id&&(stored->flags&SESSION_ARCHIVE_GAP)))value.flags|=SESSION_ARCHIVE_GAP;
            if(memcmp(stored,&value,sizeof(value))){*stored=value;dirty=true;}
            i++;slot=(slot+1)%SESSION_ARCHIVE_CAPACITY;
        }while(i<copy->count&&slot/SESSION_BLOCK_ROWS==b);
        if(dirty){
            esp_err_t err=session_stage_block(h,&next,&old,b,block.rows);
            if(err!=ESP_OK)return err;
        }
    }
    if(!memcmp(&next,&old,sizeof(next)))return ESP_OK;
    esp_err_t err=session_publish(h,&old,&next);
    if(err==ESP_OK&&gap)session_disk_gap_id=copy->records[0].id;
    return err;
}

/* Importers read one source row at a time. The complete new header is
   published only after every destination block has been written. */
typedef bool (*session_import_row_fn)(void *,unsigned,charge_session_t *);
static esp_err_t session_import(nvs_handle_t h,const session_disk_head_t *old,unsigned capacity,
                                session_import_row_fn get,void *ctx){
    session_disk_head_t next={.version=1,.next_id=old->next_id,.count=old->count,.next_slot=old->count%SESSION_ARCHIVE_CAPACITY};
    /* The main task also needs stack space for NVS page collection. */
    charge_session_t *rows=calloc(SESSION_BLOCK_ROWS,sizeof(*rows));
    if(!rows)return ESP_ERR_NO_MEM;
    esp_err_t err=ESP_OK;
    unsigned staged=0;
    for(unsigned i=0;i<old->count;i++){
        unsigned src=(old->next_slot+capacity-old->count+i)%capacity;
        unsigned j=i%SESSION_BLOCK_ROWS;
        if(!j)memset(rows,0,SESSION_BLOCK_ROWS*sizeof(*rows));
        if(!get(ctx,src,&rows[j])||!rows[j].id){err=ESP_ERR_INVALID_STATE;break;}
        if(src+1==old->active_slot)rows[j].flags|=SESSION_INTERRUPTED;
        if(j==SESSION_BLOCK_ROWS-1||i+1==old->count){
            err=session_stage_block(h,&next,old,i/SESSION_BLOCK_ROWS,rows);
            if(err!=ESP_OK)break;
            staged++;
        }
    }
    free(rows);
    if(err!=ESP_OK){
        for(unsigned b=0;b<staged;b++){
            char key[12];session_block_key(key,b,(next.banks&(UINT32_C(1)<<b))!=0);
            nvs_erase_key(h,key);
        }
        return err;
    }
    return session_publish(h,old,&next);
}
typedef struct {nvs_handle_t h;const session_disk_head_t *head;session_block_t *block;} session_disk_source_t;
static bool session_import_disk_row(void *opaque,unsigned slot,charge_session_t *out){
    session_disk_source_t *src=opaque;
    return session_disk_read(src->h,src->head,slot,src->block)&&
        session_decode_row(out,&src->block->rows[slot%SESSION_BLOCK_ROWS],false,0,0);
}
static bool session_valid_head(const session_disk_head_t *h,unsigned capacity){
    return h->count<=capacity&&h->next_slot<capacity&&h->active_slot<=capacity&&h->next_id&&
        (!h->active_slot||(h->count&&h->active_slot==(h->next_slot? h->next_slot:capacity)));
}
static int session_restore_disk(nvs_handle_t h,session_disk_head_t head,session_block_t *block){
    unsigned capacity=head.version==2?SESSION_SHORT_CAPACITY:SESSION_ARCHIVE_CAPACITY;
    if(!session_valid_head(&head,capacity))return -1;
    if(head.version==2){
        block->loaded=SESSION_BLOCKS;
        session_disk_source_t src={.h=h,.head=&head,.block=block};
        if(session_import(h,&head,capacity,session_import_disk_row,&src)!=ESP_OK)return -1;
        head=session_disk;
    }
    block->loaded=SESSION_BLOCKS;
    unsigned keep=head.count<CHARGE_SESSION_CAPACITY?head.count:CHARGE_SESSION_CAPACITY;
    sessions_init(&sessions);
    sessions.store.count=keep;sessions.store.next_slot=keep%CHARGE_SESSION_CAPACITY;sessions.store.next_id=head.next_id;
    /* Check all archive blocks once, before permitting new data to replace them. */
    for(unsigned i=0;i<head.count;i++){
        unsigned slot=session_slot_before(head.next_slot,head.count-i);
        charge_session_t row;
        if(!session_disk_read(h,&head,slot,block)||!session_decode_row(&row,&block->rows[slot%SESSION_BLOCK_ROWS],false,0,0)||!row.id)return -1;
        if(i>=head.count-keep){
            if(slot+1==head.active_slot){row.flags|=SESSION_INTERRUPTED;sessions.checkpoint=true;}
            sessions.store.records[i-(head.count-keep)]=row;
        }
    }
    /* v1 used exactly the same 512 physical slots and block encoding. */
    session_disk=head;
    session_collect_orphans(h,&head);
    nvs_erase_key(h,"sessions");
    return 1;
}
static int session_chunks_load(nvs_handle_t h){
    session_disk_head_t head;size_t n=sizeof(head);
    esp_err_t e=nvs_get_blob(h,"sess_head",&head,&n);
    if(e==ESP_ERR_NVS_NOT_FOUND){session_disk=(session_disk_head_t){.version=1,.next_id=1};return 0;}
    if(e!=ESP_OK||n!=sizeof(head)||(head.version!=1&&head.version!=2&&head.version!=3))return -1;
    session_block_t *scratch=malloc(sizeof(*scratch));
    if(!scratch)return -1;
    int result=session_restore_disk(h,head,scratch);
    free(scratch);return result;
}
