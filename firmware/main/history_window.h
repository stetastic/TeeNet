#pragma once
#include <stdint.h>

#define HISTORY_DAYS 30

/* Local calendar dates also work across 23/25-hour daylight-saving days. */
static inline unsigned history_month_days(unsigned year,unsigned month) {
    static const unsigned lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(month<1||month>12)return 0;
    return lengths[month-1]+(month==2 && year%4==0 && (year%100!=0 || year%400==0));
}
static inline int history_date_valid(uint32_t date){
    unsigned year=date/10000,month=date/100%100,day=date%100;
    return year>=1900&&year<=9999&&day>=1&&day<=history_month_days(year,month);
}
static inline uint32_t history_first_date(uint32_t today) {
    if(!history_date_valid(today))return 0;
    unsigned year=today/10000,month=today/100%100,day=today%100;
    unsigned back=HISTORY_DAYS-1;
    while(back>=day){
        back-=day;
        if(--month==0){month=12;year--;}
        day=history_month_days(year,month);
    }
    return year*10000+month*100+day-back;
}
static inline uint32_t history_next_date(uint32_t date){
    unsigned year=date/10000,month=date/100%100;
    if(date%100<history_month_days(year,month))return date+1;
    return month==12?(year+1)*10000+101:year*10000+(month+1)*100+1;
}
/* A serial calendar day determines the slot, including across leap days. */
static inline unsigned history_day_slot(uint32_t date){
    unsigned year=date/10000,month=date/100%100;
    if(month<=2){year--;month+=12;}
    unsigned serial=365*year+year/4-year/100+year/400+(153*(month-3)+2)/5+date%100-1;
    return serial%HISTORY_DAYS;
}
