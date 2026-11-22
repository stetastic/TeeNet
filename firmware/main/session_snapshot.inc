/* Keep a block reader per request. A failed read must not finish a partial export. */
typedef struct {
    session_reader_t reader;
    session_filter_t filter;
    uint32_t date, oldest_date;
    unsigned index, retained;
    bool oldest_first;
} session_view_t;

static session_view_t *session_view_open(const session_filter_t *filter, uint32_t date, bool oldest_first) {
    session_view_t *view = malloc(sizeof(*view));
    if (!view) return NULL;
    memset(view, 0, sizeof(*view));
    view->date = date;
    view->oldest_first = oldest_first;
    if (filter) view->filter = *filter;
    if (!session_reader_open(&view->reader)) {
        free(view);
        return NULL;
    }
    return view;
}

static esp_err_t session_view_next(session_view_t *view, const charge_session_t **row) {
    *row = NULL;
    while (view->index < view->reader.count) {
        unsigned index = view->oldest_first ? view->reader.count-1-view->index : view->index;
        view->index++;
        const charge_session_t *record = session_reader_at(&view->reader, index);
        if (!record) return ESP_FAIL;
        if (!session_recorded(record)) continue;
        if (!view->retained || !view->oldest_first) view->oldest_date = record->start_date;
        view->retained++;
        if (view->date ? record->start_date != view->date : !session_filter_matches(record, &view->filter)) continue;
        *row = record;
        break;
    }
    return ESP_OK;
}

static void session_view_close(session_view_t *view) {
    session_reader_close(&view->reader);
    free(view);
}
