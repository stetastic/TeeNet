#include "http_encoding.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 assert(http_accepts_gzip(NULL));assert(http_accepts_gzip("gzip"));assert(http_accepts_gzip("br, gzip;q=0.5, identity;q=0"));assert(http_accepts_gzip("GZIP ; q=1"));
 assert(!http_accepts_gzip(""));assert(!http_accepts_gzip("identity"));assert(!http_accepts_gzip("gzip;q=0,*;q=1"));assert(!http_accepts_gzip("br, *;q=0"));assert(http_accepts_gzip("br,*;q=0.5"));assert(!http_accepts_gzip("gzip;q=no"));assert(!http_accepts_gzip("gzip;q=2"));
 puts("PASS: GZIP negotiation including explicit refusal, quality and wildcard preference");return 0;
}
