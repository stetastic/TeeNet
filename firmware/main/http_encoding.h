#pragma once
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
/* No Accept-Encoding header permits any coding; an explicit refusal does not. */
static inline bool http_accepts_gzip(const char *header){
    if(!header)return true;
    int gzip=-1,wildcard=-1;
    for(const char *p=header;*p;){
        while(*p==' '||*p=='\t'||*p==',')p++;
        const char *end=strchr(p,',');if(!end)end=p+strlen(p);
        const char *name=p;while(p<end&&*p!=';'&&*p!=' '&&*p!='\t')p++;
        size_t length=(size_t)(p-name);bool coding=length==4&&tolower((unsigned char)name[0])=='g'&&tolower((unsigned char)name[1])=='z'&&tolower((unsigned char)name[2])=='i'&&tolower((unsigned char)name[3])=='p';
        bool star=length==1&&*name=='*';double q=1;
        while(p<end){
            if(*p++!=';')continue;
            while(p<end&&isspace((unsigned char)*p))p++;
            if(p<end&&tolower((unsigned char)*p)=='q'){
                p++;while(p<end&&isspace((unsigned char)*p))p++;
                if(p>=end||*p++!='='){q=0;break;}
                char *tail;q=strtod(p,&tail);if(tail==p||tail>end||q<0||q>1)q=0;
                p=tail;while(p<end&&isspace((unsigned char)*p))p++;
                if(p<end&&*p!=';')q=0;
            }
        }
        if(coding)gzip=q>0;
        if(star)wildcard=q>0;
        p=*end?end+1:end;
    }
    return (gzip>=0?gzip:wildcard)>0;
}
