#include "ems_core.h"
#include "hardware_profile.h"
#include "firmware_check.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 settings_t s;settings_defaults(&s);unsigned char image[EMS_FIRMWARE_PREFIX]={0};char version[32];
#if EXPECT_ATOM
 assert(!strcmp(EMS_HARDWARE_ID,"andreas-testboard"));
 assert(EMS_FLASH_SIZE_NIBBLE==0x30&&s.wallbox_tx_pin==2&&s.wallbox_rx_pin==1);
 assert(gpio_roles[0].standard==2&&gpio_roles[1].standard==1);
 assert(!settings_gpio_reason(2,false)[0]&&settings_gpio_reason(17,false)[0]);
 image[3]=0x3f;
#else
 assert(!strcmp(EMS_HARDWARE_ID,"esp32s3-n16r8"));
 assert(EMS_FLASH_SIZE_NIBBLE==0x40&&s.wallbox_tx_pin==17&&s.wallbox_rx_pin==18);
 image[3]=0x4f;
#endif
 assert(settings_valid(&s));
 image[0]=0xe9;image[1]=5;image[12]=9;
 memcpy(image+32,"\x32\x54\xcd\xab",4);strcpy((char *)image+48,"1.23");strcpy((char *)image+80,"wallbox_ems");
 assert(firmware_prefix_check(image,sizeof(image),version)&&!strcmp(version,"1.23"));
 image[3]^=0x70;assert(!firmware_prefix_check(image,sizeof(image),version));
 puts("PASS: ESP-IDF sdkconfig selects correct board, default UART/GPIO map and OTA flash-size validation without command-line board macros");
}
