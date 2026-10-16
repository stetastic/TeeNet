/* The server must not purge a slowly arriving remainder after an error.
   Shutdown closes the receive side before ESP-IDF's request cleanup. */
static cJSON *http_json_error(httpd_req_t *req,httpd_err_code_t code,const char *text){
    httpd_resp_set_hdr(req,"Connection","close");
    httpd_resp_send_err(req,code,text);
    int fd=httpd_req_to_sockfd(req);
    if(fd>=0)shutdown(fd,SHUT_RDWR); /* EOF lets the owner remove this session. */
    return NULL;
}
static cJSON *read_json(httpd_req_t *req){
    if(req->content_len<=0||req->content_len>8192)
        return http_json_error(req,HTTPD_400_BAD_REQUEST,"Anfrage zu gross oder leer.");
    char *body=malloc(req->content_len+1);
    if(!body)return http_json_error(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    size_t received=0;int64_t deadline=now_ms()+4000;
    while(received<req->content_len&&now_ms()<deadline){
        int n=httpd_req_recv(req,body+received,req->content_len-received);
        if(n==HTTPD_SOCK_ERR_TIMEOUT)continue;
        if(n<=0)break;
        received+=(size_t)n;
    }
    if(received!=req->content_len||now_ms()>deadline){
        free(body);return http_json_error(req,HTTPD_408_REQ_TIMEOUT,"Anfrage unvollstaendig oder zu langsam. Erneut versuchen.");
    }
    body[received]=0;
    cJSON *object=!memchr(body,0,received)?cJSON_ParseWithOpts(body,NULL,true):NULL;
    free(body);bool valid=cJSON_IsObject(object);
    if(valid)for(cJSON *i=object->child;i;i=i->next)
        for(cJSON *j=i->next;j;j=j->next)if(!strcmp(i->string,j->string))valid=false;
    if(!valid){cJSON_Delete(object);return http_json_error(req,HTTPD_400_BAD_REQUEST,"Ungueltiges JSON.");}
    return object;
}
