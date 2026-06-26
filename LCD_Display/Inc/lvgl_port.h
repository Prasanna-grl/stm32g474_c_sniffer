#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#ifdef __cplusplus
extern "C" {
#endif

void lv_port_disp_init(void);
void ui_update_values(float vbus,
                      float ibus,
                      float power,
                      float temp,
                      float cc1,
                      float cc2,
                      float plus,
                      float minus);
void lvgl_test(void);

#ifdef __cplusplus
}
#endif

#endif
