/* Copy only matching sessions in recent-first order. Metadata still describes
   the full retained history. Flash storage and its 512 slots remain unchanged. */
typedef struct {
    unsigned count,retained;
    uint32_t active_id,oldest_date;
    charge_session_t records[];
} session_snapshot_t;
static bool session_snapshot_matches(const charge_session_t *row,const session_filter_t *filter,uint32_t date){
    return date?row->start_date==date:session_filter_matches(row,filter);
}
static session_snapshot_t *session_snapshot_create(const session_filter_t *filter,uint32_t date){
    for(unsigned attempt=0;attempt<3;attempt++){
        unsigned needed=0;
        LOCK();for(unsigned i=0;i<sessions.store.count;i++)
            if(session_snapshot_matches(sessions_recent(&sessions.store,i),filter,date))needed++;
        UNLOCK();
        if(needed>CHARGE_SESSION_CAPACITY)return NULL;
        session_snapshot_t *copy=malloc(sizeof(*copy)+needed*sizeof(copy->records[0]));if(!copy)return NULL;
        copy->count=0;bool overflow=false;
        LOCK();
        copy->retained=sessions.store.count;
        copy->oldest_date=copy->retained?sessions_recent(&sessions.store,copy->retained-1)->start_date:0;
        copy->active_id=sessions.store.active_slot?sessions.store.records[sessions.store.active_slot-1].id:0;
        for(unsigned i=0;i<sessions.store.count;i++){
            const charge_session_t *row=sessions_recent(&sessions.store,i);
            if(!session_snapshot_matches(row,filter,date))continue;
            if(copy->count==needed){overflow=true;break;}
            copy->records[copy->count++]=*row;
        }
        UNLOCK();
        if(!overflow)return copy;
        free(copy); /* A new matching session appeared while allocating. */
    }
    return NULL;
}
static const charge_session_t *session_snapshot_recent(const session_snapshot_t *copy,unsigned index){
    return index<copy->count?&copy->records[index]:NULL;
}
