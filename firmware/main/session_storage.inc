/* Only changed 16-row blocks are rewritten. A bank map commits a whole snapshot
   without replacing the old 512-row blob on every checkpoint. */
#include "session_checkpoint.h"
#define SESSION_BLOCK_ROWS 16
#define SESSION_BLOCKS (CHARGE_SESSION_CAPACITY/SESSION_BLOCK_ROWS)
typedef struct {uint32_t version,count,next_slot,next_id,active_slot,valid,banks;} session_disk_head_t;
static session_disk_head_t session_disk;
static uint64_t session_hashes[SESSION_BLOCKS];
static uint64_t session_block_hash(const charge_session_t *rows){
    uint64_t h=UINT64_C(14695981039346656037);const unsigned char *p=(const unsigned char *)rows;
    for(size_t i=0;i<SESSION_BLOCK_ROWS*sizeof(*rows);i++){h^=p[i];h*=UINT64_C(1099511628211);}return h;
}
static void session_block_key(char out[12],unsigned block,bool bank){snprintf(out,12,"s%02u%c",block%SESSION_BLOCKS,bank?'b':'a');}
static int session_chunks_load(nvs_handle_t h){
    session_disk_head_t head;size_t n=sizeof(head);esp_err_t e=nvs_get_blob(h,"sess_head",&head,&n);
    if(e==ESP_ERR_NVS_NOT_FOUND)return 0;
    if(e!=ESP_OK||n!=sizeof(head)||head.version!=1||head.count>CHARGE_SESSION_CAPACITY||head.next_slot>=CHARGE_SESSION_CAPACITY||head.active_slot>CHARGE_SESSION_CAPACITY||!head.next_id)return -1;
    session_store_t *store=calloc(1,sizeof(*store));if(!store)return -1;
    store->version=2;store->count=head.count;store->next_slot=head.next_slot;store->next_id=head.next_id;store->active_slot=head.active_slot;
    for(unsigned b=0;b<SESSION_BLOCKS;b++)if(head.valid&(UINT32_C(1)<<b)){
        char key[12];session_block_key(key,b,(head.banks&(UINT32_C(1)<<b))!=0);n=SESSION_BLOCK_ROWS*sizeof(store->records[0]);
        e=nvs_get_blob(h,key,&store->records[b*SESSION_BLOCK_ROWS],&n);
        if(e!=ESP_OK||n!=SESSION_BLOCK_ROWS*sizeof(store->records[0])){free(store);return -1;}
    }
    bool okay=true;for(unsigned i=0;i<store->count;i++){unsigned slot=(store->next_slot+CHARGE_SESSION_CAPACITY-1-i)%CHARGE_SESSION_CAPACITY;if(!(head.valid&(UINT32_C(1)<<(slot/SESSION_BLOCK_ROWS)))||!store->records[slot].id)okay=false;}
    okay=okay&&sessions_restore(&sessions,store,sizeof(*store),settings.price_kwh,settings.solar_price_kwh);
    if(okay){session_disk=head;for(unsigned b=0;b<SESSION_BLOCKS;b++)session_hashes[b]=session_block_hash(&store->records[b*SESSION_BLOCK_ROWS]);}
    free(store);if(!okay)return -1;
    return 1;
}
static esp_err_t session_chunks_save_source(nvs_handle_t h,const session_store_t *store,const session_checkpoint_t *copy){
    session_disk_head_t next={.version=1,.count=copy?copy->count:store->count,.next_slot=copy?copy->next_slot:store->next_slot,.next_id=copy?copy->next_id:store->next_id,.active_slot=copy?copy->active_slot:store->active_slot,.valid=session_disk.valid,.banks=session_disk.banks};
    uint64_t hashes[SESSION_BLOCKS];charge_session_t block[SESSION_BLOCK_ROWS];
    uint32_t changed=0;esp_err_t e=ESP_OK;
    for(unsigned b=0;b<SESSION_BLOCKS;b++){
        if(copy){for(unsigned j=0;j<SESSION_BLOCK_ROWS;j++){const charge_session_t *row=session_checkpoint_at(copy,b*SESSION_BLOCK_ROWS+j);block[j]=row?*row:(charge_session_t){0};}}
        else memcpy(block,&store->records[b*SESSION_BLOCK_ROWS],sizeof(block));
        const charge_session_t *rows=block;uint64_t hash=session_block_hash(rows);hashes[b]=hash;
        bool any=false;for(unsigned j=0;j<SESSION_BLOCK_ROWS;j++)if(rows[j].id){any=true;break;}
        uint32_t bit=UINT32_C(1)<<b;
        if(!any){next.valid&=~bit;continue;}
        if((session_disk.valid&bit)&&session_disk.version==1&&session_hashes[b]==hash)continue;
        bool bank=(session_disk.valid&bit)?!(session_disk.banks&bit):false;char key[12];session_block_key(key,b,bank);
        e=nvs_set_blob(h,key,rows,SESSION_BLOCK_ROWS*sizeof(*rows));if(e!=ESP_OK)return e;
        changed|=bit;next.valid|=bit;if(bank)next.banks|=bit;else next.banks&=~bit;
    }
    e=nvs_set_blob(h,"sess_head",&next,sizeof(next));if(e==ESP_OK)e=nvs_commit(h);if(e!=ESP_OK)return e;
    for(unsigned b=0;b<SESSION_BLOCKS;b++){
        uint32_t bit=UINT32_C(1)<<b;
        if((changed&bit)&&(session_disk.valid&bit)){char key[12];session_block_key(key,b,(session_disk.banks&bit)!=0);nvs_erase_key(h,key);}
        if(!(next.valid&bit)&&(session_disk.valid&bit)){char key[12];session_block_key(key,b,false);nvs_erase_key(h,key);session_block_key(key,b,true);nvs_erase_key(h,key);}
        session_hashes[b]=hashes[b];
    }
    session_disk=next;nvs_erase_key(h,"sessions");return ESP_OK;
}
static inline esp_err_t session_chunks_save(nvs_handle_t h,const session_store_t *store){return session_chunks_save_source(h,store,NULL);}
static inline esp_err_t session_chunks_save_checkpoint(nvs_handle_t h,const session_checkpoint_t *copy){return session_chunks_save_source(h,NULL,copy);}
