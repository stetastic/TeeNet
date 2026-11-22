/* One archive block per reader. Take the storage lock only when opening the
   reader or copying a block, never while sending a response over the network. */
typedef struct {
    unsigned count,overlay_count,added;
    uint32_t active_id,revision;
    session_disk_head_t head;
    session_block_t block;
    charge_session_t overlay[CHARGE_SESSION_CAPACITY];
    nvs_handle_t nvs;
    bool opened;
} session_reader_t;
static bool session_reader_open(session_reader_t *r){
    memset(r,0,sizeof(*r));r->block.loaded=SESSION_BLOCKS;
    if(xSemaphoreTake(status_snapshot_lock,ticks(1000))!=pdTRUE)return false;
    LOCK();
    bool ok=stats_storage_ok;
    r->head=session_disk;r->revision=session_disk_revision;
    r->overlay_count=sessions.store.count;
    if(r->overlay_count>CHARGE_SESSION_CAPACITY)ok=false;
    if(ok){
        for(unsigned i=0;i<r->overlay_count;i++)r->overlay[i]=*sessions_recent(&sessions.store,i);
        if(sessions.store.active_slot)r->active_id=sessions.store.records[sessions.store.active_slot-1].id;
        bool gap=false;
        ok=session_appended_count(&r->head,sessions.store.next_id,r->overlay_count,&r->added,&gap);
        if(gap&&r->overlay_count)r->overlay[r->overlay_count-1].flags|=SESSION_ARCHIVE_GAP;
        for(unsigned i=0;i<r->overlay_count;i++)if(r->overlay[i].id==session_disk_gap_id)r->overlay[i].flags|=SESSION_ARCHIVE_GAP;
    }
    UNLOCK();
    if(ok){
        r->count=r->head.count+r->added;
        if(r->count>SESSION_ARCHIVE_CAPACITY)r->count=SESSION_ARCHIVE_CAPACITY;
        if(r->count>r->overlay_count){
            r->opened=nvs_open_from_partition("stats","energy",NVS_READONLY,&r->nvs)==ESP_OK;
            ok=r->opened;
        }
    }
    xSemaphoreGive(status_snapshot_lock);
    return ok;
}
static const charge_session_t *session_reader_at(session_reader_t *r,unsigned index){
    if(index>=r->count)return NULL;
    if(index<r->overlay_count)return &r->overlay[index];
    unsigned slot=session_slot_before(r->head.next_slot,index-r->added+1);
    if(r->block.loaded!=slot/SESSION_BLOCK_ROWS){
        if(xSemaphoreTake(status_snapshot_lock,ticks(1000))!=pdTRUE)return NULL;
        bool ok=r->revision==session_disk_revision&&session_disk_read(r->nvs,&r->head,slot,&r->block);
        xSemaphoreGive(status_snapshot_lock);
        if(!ok)return NULL;
    }
    return &r->block.rows[slot%SESSION_BLOCK_ROWS];
}
static void session_reader_close(session_reader_t *r){
    if(r->opened){nvs_close(r->nvs);r->opened=false;}
}
