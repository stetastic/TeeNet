#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Boot-only reader for the old single NVS blob. ESP-IDF's public nvs_get_blob
   requires a RAM destination as large as the entire value. Read the mapped
   stats partition instead. The NVS library has already recovered active pages.
   Only live entries in the energy namespace, with valid header and payload CRC,
   participate. Use the published blob index to select the committed bank. */
typedef struct {
    const uint8_t *base;size_t bytes;
    const uint8_t *pieces[16];uint16_t lengths[16];
    unsigned pieces_used;size_t total;
} session_legacy_blob_t;
static uint32_t session_u32(const uint8_t *p){uint32_t v;memcpy(&v,p,sizeof(v));return v;}
static uint16_t session_u16(const uint8_t *p){uint16_t v;memcpy(&v,p,sizeof(v));return v;}
static const uint8_t *session_nvs_item(const session_legacy_blob_t *blob,uint8_t ns,uint8_t type,
                                       const char *key,int chunk,unsigned *available){
    const uint8_t *found=NULL;
    for(size_t offset=0;offset+4096<=blob->bytes;offset+=4096){
        const uint8_t *page=blob->base+offset;
        uint32_t state=session_u32(page);
        if(state!=UINT32_C(0xfffffffe)&&state!=UINT32_C(0xfffffffc))continue;
        for(unsigned i=0;i<126;){
            unsigned live=(page[32+i/4]>>((i%4)*2))&3;
            if(live!=2){i++;continue;}
            const uint8_t *entry=page+64+32*i;
            unsigned span=entry[2];
            if(!span||span>126-i)return NULL;
            if(entry[0]==ns&&entry[1]==type&&!memcmp(entry+8,key,strlen(key)+1)&&
               (chunk<0||entry[3]==chunk)){
                uint32_t crc=esp_rom_crc32_le(UINT32_MAX,entry,4);
                crc=esp_rom_crc32_le(crc,entry+8,24);
                if(found||crc!=session_u32(entry+4))return NULL;
                found=entry;*available=(span-1)*32;
            }
            i+=span;
        }
    }
    return found;
}
static bool session_blob_index(session_legacy_blob_t *blob,size_t expected){
    unsigned avail=0;
    const uint8_t *ns=session_nvs_item(blob,0,1,"energy",-1,&avail);
    if(!ns||!ns[24]||ns[24]==255)return false;
    const uint8_t *idx=session_nvs_item(blob,ns[24],0x48,"sessions",255,&avail);
    unsigned count=1,start=0;
    if(idx){
        count=idx[28];start=idx[29];
        if(session_u32(idx+24)!=expected||!count||count>16||(start!=0&&start!=128))return false;
    }
    size_t total=0;
    for(unsigned i=0;i<count;i++){
        const uint8_t *item=session_nvs_item(blob,ns[24],idx?0x42:0x41,"sessions",idx?(int)(start+i):-1,&avail);
        if(!item)return false;
        unsigned len=session_u16(item+24);
        if(len>avail||esp_rom_crc32_le(UINT32_MAX,item+32,len)!=session_u32(item+28))return false;
        blob->pieces[i]=item+32;blob->lengths[i]=(uint16_t)len;total+=len;
    }
    blob->pieces_used=count;blob->total=total;
    return total==expected;
}
static bool session_blob_slice(const session_legacy_blob_t *blob,size_t offset,void *out,size_t len){
    if(offset>blob->total||len>blob->total-offset)return false;
    uint8_t *p=out;
    for(unsigned i=0;i<blob->pieces_used&&len;i++){
        size_t n=blob->lengths[i];
        if(offset>=n){offset-=n;continue;}
        n-=offset;if(n>len)n=len;
        memcpy(p,blob->pieces[i]+offset,n);p+=n;len-=n;offset=0;
    }
    return !len;
}
