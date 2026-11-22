#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "session_monolith_import.inc"

static int session_monolith_load(nvs_handle_t h){
    size_t size=0;
    esp_err_t err=nvs_get_blob(h,"sessions",NULL,&size);
    if(err==ESP_ERR_NVS_NOT_FOUND)return 0;
    const size_t header=offsetof(session_store_t,records);
    if(err!=ESP_OK||size<header||size>header+SESSION_ARCHIVE_CAPACITY*sizeof(charge_session_t))return -1;
    const esp_partition_t *part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,"stats");
    if(!part)return -1;
    const void *base;esp_partition_mmap_handle_t mapping;
    if(esp_partition_mmap(part,0,part->size,ESP_PARTITION_MMAP_DATA,&base,&mapping)!=ESP_OK)return -1;
    err=session_import_monolith(h,base,part->size,size);
    esp_partition_munmap(mapping);
    if(err!=ESP_OK){
        ESP_LOGE(TAG,"Session migration: %s; original sessions blob retained",esp_err_to_name(err));
        return -1;
    }
    /* A power loss after publishing the new header can leave a redundant
       original. session_chunks_load also removes it after verifying the ring. */
    return session_chunks_load(h);
}
