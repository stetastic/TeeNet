/* ESP-IDF purges an unread request body after a handler returns. Even a
   rejected POST can otherwise keep its single HTTP task busy indefinitely
   by supplying one byte before each receive timeout. POSTs are infrequent;
   close their connection after the response, including successful uploads. */
static esp_err_t http_post_guard(httpd_req_t *req){
    const httpd_uri_t *route=req->user_ctx;
    httpd_resp_set_hdr(req,"Connection","close");
    esp_err_t result=route->handler(req);
    int fd=httpd_req_to_sockfd(req);
    if(fd>=0)shutdown(fd,SHUT_RDWR);
    return result;
}
