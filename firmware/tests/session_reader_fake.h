/* Logical flash rows, separate from the HTTP heap allocation tracker. */
static charge_session_t flash_rows[512];
static unsigned flash_count, readers;
static int bad_row=-1;
static bool reject_open;
typedef struct {
    unsigned count;
    uint32_t active_id;
} session_reader_t;
static uint32_t active_id;
static bool session_reader_open(session_reader_t *r){
    if(reject_open)return false;
    r->count=flash_count;r->active_id=active_id;readers++;
    return true;
}
static const charge_session_t *session_reader_at(session_reader_t *r,unsigned i){
    assert(i<r->count);
    return (int)i==bad_row?NULL:&flash_rows[r->count-1-i];
}
static void session_reader_close(session_reader_t *r){(void)r;assert(readers);readers--;}
