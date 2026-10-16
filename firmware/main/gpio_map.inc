#define GPIO_ROLE(key,label,help,standard) {#key,label,help,offsetof(settings_t,key),standard}
const gpio_role_t gpio_roles[EMS_GPIO_ROLES]={
    GPIO_ROLE(wallbox_tx_pin,"Shell TX","Daten vom ESP zur Shell.",EMS_DEFAULT_SHELL_TX_PIN),
    GPIO_ROLE(wallbox_rx_pin,"Shell RX","Daten von der Shell zum ESP.",EMS_DEFAULT_SHELL_RX_PIN),
    GPIO_ROLE(xemex_tx_pin,"Wallbox-Zähler TX","Daten zum seriellen Wallbox-Zähler.",EMS_DEFAULT_METER_TX_PIN),
    GPIO_ROLE(xemex_rx_pin,"Wallbox-Zähler RX","Messwerte vom seriellen Wallbox-Zähler.",EMS_DEFAULT_METER_RX_PIN),
    GPIO_ROLE(house_tx_pin,"Hauszähler TX","Daten zum seriellen Hausanschlusszähler.",EMS_DEFAULT_HOUSE_TX_PIN),
    GPIO_ROLE(house_rx_pin,"Hauszähler RX","Messwerte vom seriellen Hausanschlusszähler.",EMS_DEFAULT_HOUSE_RX_PIN),
    GPIO_ROLE(external_mode_input_pin,"Manuell / PV-Kontakt","Kontakt gegen GND wählt PV-Überschuss.",EMS_DEFAULT_MODE_INPUT_PIN),
    GPIO_ROLE(evu_input_pin,"Extern Start / Stopp / EVU","Kontakt gegen GND stoppt oder begrenzt die Ladung.",7),
    GPIO_ROLE(relay1_pin,"Phasenschütz-Ausgang","3,3 V / High schaltet das Relais ein.",EMS_DEFAULT_RELAY_PIN),
    GPIO_ROLE(phase_feedback_pin,"Schütz-Rückmeldung 1","Kontakt gegen GND; bestätigte Stellung unter Phasenumschaltung wählen.",EMS_DEFAULT_FEEDBACK_PIN),
    GPIO_ROLE(phase_feedback_second_pin,"Schütz-Rückmeldung 2","Zweiter Kontakt gegen GND bestätigt einphasig bei zwei Rückmeldeeingängen.",EMS_DEFAULT_FEEDBACK_SECOND_PIN),
    GPIO_ROLE(status_led_pin,"RGB-LED","Schaltet die adressierbare LED beim Start aus; -1 deaktiviert dies.",EMS_DEFAULT_LED_PIN)
};
#undef GPIO_ROLE
int settings_gpio_value(const settings_t *s,unsigned role){return *(const int *)((const char *)s+gpio_roles[role].offset);}
void settings_gpio_set(settings_t *s,unsigned role,int pin){*(int *)((char *)s+gpio_roles[role].offset)=pin;}
const char *settings_gpio_reason(int pin,bool led){
    if(led&&pin==-1)return "";
    if(pin<0||pin>48||(pin>=22&&pin<=25))return "Kein GPIO";
    if(pin==19||pin==20)return "USB";
    if(pin==0||pin==3||pin==45||pin==46)return "Start / Boot";
#if defined(CONFIG_TEE_BOARD_ATOMS3_LITE) && CONFIG_TEE_BOARD_ATOMS3_LITE
    if(pin==35)return led?"":"RGB-LED am Board";
    if(pin==1||pin==2||(pin>=5&&pin<=8)||pin==38||pin==39)return "";
    if(pin>=26&&pin<=32)return "Flash-Speicher";
    return "Am Board nicht frei herausgeführt";
#else
    if(pin>=26&&pin<=37)return "Flash / PSRAM";
    if(pin==48)return led?"":"RGB-LED am Board";
    return "";
#endif
}
bool settings_gpio_used(const settings_t *s,unsigned role){
    bool meter=s->meter_rs485_interface==0&&strcmp(s->wallbox_meter_type,"shelly_gen2")&&strcmp(s->wallbox_meter_type,"shelly_em1")&&strcmp(s->wallbox_meter_type,"em24_tcp");
    bool house=s->house_rs485_interface==0&&(!strcmp(s->house_meter_type,"xemex")||sdm_profile(s->house_meter_type)!=NULL);
    if(role<2)return s->shell_rs485_interface==0;
    if(role<4)return meter;
    if(role<6)return house;
    if(role==6)return s->external_mode_input_enabled;
    if(role==7)return s->evu_input_enabled;
    if(role==8)return s->relay_board_enabled&&s->phase_actuator_type==0;
    if(role==9)return s->phase_switch_enabled&&s->phase_feedback_enabled&&s->phase_actuator_type==0;
    if(role==10)return s->phase_switch_enabled&&s->phase_feedback_count==2&&s->phase_actuator_type==0;
    return s->status_led_pin!=-1;
}
bool settings_gpio_valid(const settings_t *s){
    for(unsigned i=0;i<EMS_GPIO_ROLES;i++){
        int pin=settings_gpio_value(s,i);
        if(settings_gpio_reason(pin,i==11)[0])return false;
        if(!settings_gpio_used(s,i))continue;
        for(unsigned j=0;j<i;j++)if(settings_gpio_used(s,j)&&pin==settings_gpio_value(s,j))return false;
    }
    return true;
}
bool settings_gpio_modified(const settings_t *s){
    for(unsigned i=0;i<EMS_GPIO_ROLES;i++)if(settings_gpio_value(s,i)!=gpio_roles[i].standard)return true;
    return false;
}
bool settings_gpio_equal(const settings_t *a,const settings_t *b){
    for(unsigned i=0;i<EMS_GPIO_ROLES;i++)if(settings_gpio_value(a,i)!=settings_gpio_value(b,i))return false;
    return true;
}
