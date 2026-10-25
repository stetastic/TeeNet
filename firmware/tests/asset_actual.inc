static esp_err_t asset_send(httpd_req_t *req) {

    const uint8_t *start=html_gz_start,*end=html_gz_end; const char *type="text/html; charset=utf-8";

    if(!strcmp(req->uri,"/dashboard.css")) start=css_gz_start,end=css_gz_end,type="text/css; charset=utf-8";

    if(!strcmp(req->uri,"/dashboard.js")) start=js_gz_start,end=js_gz_end,type="application/javascript; charset=utf-8";
    if(!strcmp(req->uri,"/energy.js")) start=energy_gz_start,end=energy_gz_end,type="application/javascript; charset=utf-8";
    if(!strcmp(req->uri,"/health.js")) start=health_gz_start,end=health_gz_end,type="application/javascript; charset=utf-8";
    if(!strcmp(req->uri,"/firmware.js")) start=firmware_gz_start,end=firmware_gz_end,type="application/javascript; charset=utf-8";
    if(!strcmp(req->uri,"/terms.js")) start=terms_js_gz_start,end=terms_js_gz_end,type="application/javascript; charset=utf-8";
    else if(!strcmp(req->uri,"/evcc-test.js")) start=evcc_js_gz_start,end=evcc_js_gz_end,type="application/javascript; charset=utf-8";
    if(!strcmp(req->uri,"/features.js")) start=features_gz_start,end=features_gz_end,type="application/javascript; charset=utf-8";
    if(!strcmp(req->uri,"/qrcode.js")) start=qr_gz_start,end=qr_gz_end,type="application/javascript; charset=utf-8";
    if(!strcmp(req->uri,"/guide.js")) start=guide_js_gz_start,end=guide_js_gz_end,type="application/javascript; charset=utf-8";
    if(!strcmp(req->uri,"/expert-schema.json")) start=expert_schema_gz_start,end=expert_schema_gz_end,type="application/json; charset=utf-8";

    if(!strcmp(req->uri,"/teennet-logo.png")) start=logo_start,end=logo_end,type="image/png";

    if(!strcmp(req->uri,"/hardware-plan.svg")) start=hardware_start,end=hardware_end,type="image/svg+xml";
    if(!strcmp(req->uri,"/wiring-plan.jpg")) start=wiring_start,end=wiring_end,type="image/jpeg";


    if(!strcmp(req->uri,"/guide")) start=guide_gz_start,end=guide_gz_end,type="text/html; charset=utf-8";

    bool gzip=start!=logo_start&&start!=hardware_start&&start!=wiring_start;
    size_t length=(size_t)(end-start)-(gzip?0:1);
    if(gzip){
        char encoding[256];esp_err_t header=httpd_req_get_hdr_value_str(req,"Accept-Encoding",encoding,sizeof(encoding));
        if((header!=ESP_OK&&header!=ESP_ERR_NOT_FOUND)||!http_accepts_gzip(header==ESP_OK?encoding:NULL)){
            httpd_resp_set_hdr(req,"Vary","Accept-Encoding");httpd_resp_set_status(req,"406 Not Acceptable");
            return httpd_resp_sendstr(req,"Diese Oberflaeche benoetigt einen Browser mit GZIP-Unterstuetzung. Die JSON-API bleibt verfuegbar.");
        }
        httpd_resp_set_hdr(req,"Content-Encoding","gzip");
        httpd_resp_set_hdr(req,"Vary","Accept-Encoding");
    }

    char etag[128],previous_etag[128];
    snprintf(etag,sizeof(etag),"\"%s:%.80s:%s\"",EMS_BUILD_ID,req->uri,gzip?"gzip":"plain");
    httpd_resp_set_hdr(req,"Cache-Control","no-cache");
    httpd_resp_set_hdr(req,"ETag",etag);
    if(httpd_req_get_hdr_value_str(req,"If-None-Match",previous_etag,sizeof(previous_etag))==ESP_OK && !strcmp(previous_etag,etag)) {
        httpd_resp_set_status(req,"304 Not Modified");
        return httpd_resp_send(req,NULL,0);
    }

    httpd_resp_set_type(req,type); httpd_resp_set_hdr(req,"Cache-Control","no-cache"); httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");

    httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'");

    /* Content-Length avoids three small TCP writes for every 2 KB chunk.
       Large pictures run on a separate worker, so slow image downloads cannot
       block status, control, or the scripts needed to start the dashboard. */
    return httpd_resp_send(req,(const char *)start,length);

}

