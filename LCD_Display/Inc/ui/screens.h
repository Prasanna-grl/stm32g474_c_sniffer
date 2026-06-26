#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <../../lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
  lv_obj_t *main;
  lv_obj_t *main_container;
  lv_obj_t *obj0;
  lv_obj_t *vbus_lable_value;
  lv_obj_t *v;
  lv_obj_t *vbus;
  lv_obj_t *obj1;
  lv_obj_t *ibus;
  lv_obj_t *ibus_label_value;
  lv_obj_t *a;
  lv_obj_t *obj2;
  lv_obj_t *pwr;
  lv_obj_t *pwr_label_value;
  lv_obj_t *w;
  lv_obj_t *t_value;
  lv_obj_t *t_logo;
  lv_obj_t *plus_value;
  lv_obj_t *minus_val;
  lv_obj_t *cc2;
  lv_obj_t *cc1_img;
  lv_obj_t *cc1_val;
  lv_obj_t *cc_two_val;
  lv_obj_t *minus_logo;
  lv_obj_t *plus_logo;
  lv_obj_t *cc1_logo;
} objects_t;

extern objects_t objects;

enum ScreensEnum {
  SCREEN_ID_MAIN = 1,
};

void create_screen_main();
void tick_screen_main();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/
