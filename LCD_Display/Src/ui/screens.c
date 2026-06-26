#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t objects;
lv_obj_t *tick_value_change_obj;
uint32_t active_theme_index = 0;

void create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 240, 135);
    {
        lv_obj_t *parent_obj = obj;
        {
            // main_container
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.main_container = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 240, 135);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_obj_create(parent_obj);
                    objects.obj0 = obj;
                    lv_obj_set_pos(obj, 3, 2);
                    lv_obj_set_size(obj, 222, 30);
                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0d0d0d), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_opa(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_color(obj, lv_color_hex(0xffdcb72a), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // vbus_lable_value
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.vbus_lable_value = obj;
                            lv_obj_set_pos(obj, 56, 1);
                            lv_obj_set_size(obj, 133, 25);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffcae55a), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_letter_space(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_line_space(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, " 27.2256");
                        }
                        {
                            // v
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.v = obj;
                            lv_obj_set_pos(obj, 198, 2);
                            lv_obj_set_size(obj, 13, 24);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "V");
                        }
                        {
                            // vbus
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.vbus = obj;
                            lv_obj_set_pos(obj, 3, 5);
                            lv_obj_set_size(obj, 47, 16);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "VBUS");
                        }
                    }
                }
                {
                    lv_obj_t *obj = lv_obj_create(parent_obj);
                    objects.obj1 = obj;
                    lv_obj_set_pos(obj, 3, 35);
                    lv_obj_set_size(obj, 222, 30);
                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_color(obj, lv_color_hex(0xff4cb313), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // ibus
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.ibus = obj;
                            lv_obj_set_pos(obj, 3, 5);
                            lv_obj_set_size(obj, 47, 16);
                            lv_obj_set_style_border_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_border_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xfff1f1f1), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "IBUS");
                        }
                        {
                            // ibus_label_value
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.ibus_label_value = obj;
                            lv_obj_set_pos(obj, 56, 1);
                            lv_obj_set_size(obj, 133, 25);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffa8ff31), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_letter_space(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_line_space(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "4.78420");
                        }
                        {
                            // a
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.a = obj;
                            lv_obj_set_pos(obj, 195, 0);
                            lv_obj_set_size(obj, 20, 24);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, " A");
                        }
                    }
                }
                {
                    lv_obj_t *obj = lv_obj_create(parent_obj);
                    objects.obj2 = obj;
                    lv_obj_set_pos(obj, 3, 68);
                    lv_obj_set_size(obj, 222, 30);
                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_color(obj, lv_color_hex(0xff00e1ff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // pwr
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.pwr = obj;
                            lv_obj_set_pos(obj, 3, 5);
                            lv_obj_set_size(obj, 39, 15);
                            lv_obj_set_style_border_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_border_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xfff1f1f1), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "PWR");
                        }
                        {
                            // pwr_label_value
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.pwr_label_value = obj;
                            lv_obj_set_pos(obj, 56, 1);
                            lv_obj_set_size(obj, 133, 25);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xff02e2ff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_letter_space(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_line_space(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "130.253");
                        }
                        {
                            // w
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.w = obj;
                            lv_obj_set_pos(obj, 197, 2);
                            lv_obj_set_size(obj, 23, 24);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "W");
                        }
                    }
                }
                {
                    // t_value
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.t_value = obj;
                    lv_obj_set_pos(obj, 182, 108);
                    lv_obj_set_size(obj, 40, 15);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "011. 147");
                }
                {
                    // t_logo
                    lv_obj_t *obj = lv_img_create(parent_obj);
                    objects.t_logo = obj;
                    lv_obj_set_pos(obj, 165, 109);
                    lv_obj_set_size(obj, 15, 15);
                    lv_img_set_src(obj, &img_t);
                }
            }
        }
        {
            // plus_value
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.plus_value = obj;
            lv_obj_set_pos(obj, 31, 103);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xfffefefe), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "0.00V");
        }
        {
            // minus_val
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.minus_val = obj;
            lv_obj_set_pos(obj, 31, 120);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "0.00V");
        }
        {
            // cc2
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.cc2 = obj;
            lv_obj_set_pos(obj, 89, 119);
            lv_obj_set_size(obj, 28, 15);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xfff6f6f6), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "CC2:");
        }
        {
            // cc1_img
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.cc1_img = obj;
            lv_obj_set_pos(obj, 89, 104);
            lv_obj_set_size(obj, 28, 15);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "CC1:");
        }
        {
            // cc1_val
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.cc1_val = obj;
            lv_obj_set_pos(obj, 120, 103);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xfff6f6f6), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "0.00V");
        }
        {
            // cc_two_val
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.cc_two_val = obj;
            lv_obj_set_pos(obj, 120, 119);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xfff6f6f6), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "0.00V");
        }
        {
            // minus_logo
            lv_obj_t *obj = lv_img_create(parent_obj);
            objects.minus_logo = obj;
            lv_obj_set_pos(obj, 16, 123);
            lv_obj_set_size(obj, 10, 10);
            lv_img_set_src(obj, &img_minus);
        }
        {
            // plus_logo
            lv_obj_t *obj = lv_img_create(parent_obj);
            objects.plus_logo = obj;
            lv_obj_set_pos(obj, 16, 106);
            lv_obj_set_size(obj, 10, 10);
            lv_img_set_src(obj, &img_plus);
        }
        {
            // cc1_logo
            lv_obj_t *obj = lv_img_create(parent_obj);
            objects.cc1_logo = obj;
            lv_obj_set_pos(obj, 78, 106);
            lv_obj_set_size(obj, 10, 10);
            lv_img_set_src(obj, &img_cc1_logo);
        }
        {
            lv_obj_t *obj = lv_img_create(parent_obj);
            lv_obj_set_pos(obj, 78, 123);
            lv_obj_set_size(obj, 10, 10);
            lv_img_set_src(obj, &img_cc1_logo);
        }
    }
    
    tick_screen_main();
}

void tick_screen_main() {
}



typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_main,
};
void tick_screen(int screen_index) {
    tick_screen_funcs[screen_index]();
}
void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen_funcs[screenId - 1]();
}

void create_screens() {
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    create_screen_main();
}
