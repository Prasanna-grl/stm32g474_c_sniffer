#include "lvgl.h"
#include "st7789.h"
#include "main.h"
#include <stdio.h>
#include "screens.h"

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[ST7789_WIDTH * 20];   // buffer height = 20 lines

/* 🔴 THIS IS THE FLUSH CALLBACK */
static void st7789_flush_cb(lv_disp_drv_t *disp,
                            const lv_area_t *area,
                            lv_color_t *color_p)
{
    uint16_t w = area->x2 - area->x1 + 1;
    uint16_t h = area->y2 - area->y1 + 1;
   ST7789_DrawImage(area->x1, area->y1, w, h, (uint16_t *)color_p);

    lv_disp_flush_ready(disp);  // VERY IMPORTANT
}

void lvgl_port_init(void)
{
    lv_init();

    lv_disp_draw_buf_init(&draw_buf, buf1, NULL,
                           ST7789_WIDTH * 20);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);

    disp_drv.hor_res = ST7789_WIDTH;
    disp_drv.ver_res = ST7789_HEIGHT;
    disp_drv.flush_cb = st7789_flush_cb;
    disp_drv.draw_buf = &draw_buf;

    lv_disp_drv_register(&disp_drv);
}

void ui_update_values(float vbus,
                      float ibus,
                      float power,
                      float temp,
                      float cc1,
                      float cc2,
                      float plus,
                      float minus)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%.4f", vbus);
    lv_label_set_text(objects.vbus_lable_value, buf);

    snprintf(buf, sizeof(buf), "%.4f", ibus);
    lv_label_set_text(objects.ibus_label_value, buf);

    snprintf(buf, sizeof(buf), "%.3f", power);
    lv_label_set_text(objects.pwr_label_value, buf);

    snprintf(buf, sizeof(buf), "%.2f", temp);
    lv_label_set_text(objects.t_value, buf);

    snprintf(buf, sizeof(buf), "%.2fV", cc1);
    lv_label_set_text(objects.cc1_val, buf);

    snprintf(buf, sizeof(buf), "%.2fV", cc2);
    lv_label_set_text(objects.cc_two_val, buf);

    snprintf(buf, sizeof(buf), "%.2fV", plus);
    lv_label_set_text(objects.plus_value, buf);

    snprintf(buf, sizeof(buf), "%.2fV", minus);
    lv_label_set_text(objects.minus_val, buf);
}

void lvgl_test(void)
{
	  static float vbus = 0.0000f;
	  static float ibus = 0.0000f;
	  static float temp = 0.000f;
	  static float cc1  = 0.00f;
	  static float cc2  = 0.00;
	  static float plus = 0.00f;
	  static float minus = 0.00f;


	  vbus += 0.4f;
	  ibus += 0.3f;
	  temp += 0.2f;
	  cc1  += 0.02f;
	  cc2  += 0.015f;
	  plus += 0.01f;
	  minus += 0.008f;

	  if (vbus > 99) vbus = 1;
	  if (ibus > 99) ibus = 1;
	  if (temp > 99) temp = 1;
	  if (cc1 > 5) cc1 = 0;
	  if (cc2 > 5) cc2 = 0;

	  float power = vbus * ibus;

	  ui_update_values(vbus, ibus, power, temp,
	                   cc1, cc2, plus, minus);

	  lv_timer_handler();
//	  HAL_Delay(500);
}
