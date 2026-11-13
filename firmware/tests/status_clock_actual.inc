/* Generated status_task clock statements; do not edit. */
static int64_t status_decision_clock_fixture(void){
        int64_t epoch,midnight; uint32_t date=calendar(&epoch,&midnight); LOCK();
        /* A producer may publish while this task waits for state_lock.
           Freshness must use a decision time no older than that snapshot. */
        int64_t now=now_ms();
    (void)epoch;(void)midnight;(void)date;return now;
}
static int64_t status_reset_clock_fixture(int64_t now){
        if(reset_count_pending && now>=reset_clear_at && !backup_in_progress) { UNLOCK(); reset_sequence_clear(); LOCK(); now=now_ms(); }
    return now;
}
