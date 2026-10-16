#pragma once
/* Included after settings_t. Only pins usable on the selected board are assignable. */
#define EMS_GPIO_ROLES 12
typedef struct { const char *id,*label,*help; size_t offset; int standard; } gpio_role_t;
extern const gpio_role_t gpio_roles[EMS_GPIO_ROLES];
int settings_gpio_value(const settings_t *s,unsigned role);
void settings_gpio_set(settings_t *s,unsigned role,int pin);
bool settings_gpio_used(const settings_t *s,unsigned role);
const char *settings_gpio_reason(int pin,bool led);
bool settings_gpio_valid(const settings_t *s);
bool settings_gpio_modified(const settings_t *s);
bool settings_gpio_equal(const settings_t *a,const settings_t *b);
