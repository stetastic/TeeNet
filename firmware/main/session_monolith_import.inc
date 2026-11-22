#include "session_legacy_blob.h"

typedef struct {
    session_legacy_blob_t blob;
    size_t size,row_bytes;
    unsigned reads;
    bool legacy;
} session_monolith_t;
static bool session_monolith_row(void *opaque,unsigned slot,charge_session_t *out){
    session_monolith_t *src=opaque;
    /* Each destination block write can cause NVS garbage collection. Rescan
       before the next batch so relocated source chunks are always read live. */
    if(src->reads%SESSION_BLOCK_ROWS==0&&!session_blob_index(&src->blob,src->size))return false;
    src->reads++;
    charge_session_t raw;
    return session_blob_slice(&src->blob,offsetof(session_store_t,records)+slot*src->row_bytes,&raw,src->row_bytes)&&
        session_decode_row(out,&raw,src->legacy,settings.price_kwh,settings.solar_price_kwh);
}
static esp_err_t session_import_monolith(nvs_handle_t h,const uint8_t *base,size_t bytes,size_t size){
    const size_t header=offsetof(session_store_t,records);
    session_monolith_t src={.blob={.base=base,.bytes=bytes},.size=size};
    session_disk_head_t old={0};
    _Static_assert(offsetof(session_store_t,records)==offsetof(session_disk_head_t,valid),"old session header layout");
    esp_err_t err=ESP_ERR_INVALID_STATE;
    if(session_blob_index(&src.blob,size)&&session_blob_slice(&src.blob,0,&old,header)){
        src.legacy=old.version==1;
        src.row_bytes=src.legacy?sizeof(session_v1_t):sizeof(charge_session_t);
        unsigned capacity=(size-header)/src.row_bytes;
        if((src.legacy||old.version==2)&&(capacity==SESSION_SHORT_CAPACITY||capacity==SESSION_ARCHIVE_CAPACITY)&&
           size==header+capacity*src.row_bytes&&session_valid_head(&old,capacity))
            err=session_import(h,&old,capacity,session_monolith_row,&src);
    }
    return err;
}
