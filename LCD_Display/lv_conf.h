#ifndef LV_CONF_H_
#define LV_CONF_H_

/*====================
 * GENERAL SETTINGS
 *====================*/
#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   1

#define LV_MEM_CUSTOM      0
#define LV_USE_OS          0

/*====================
 * MEMORY SETTINGS
 *====================*/
#define LV_MEM_SIZE (36U * 1024U)
#define LV_MEMCPY_MEMSET_STD 1

/*====================
 * DISPLAY SETTINGS
 *====================*/
#define LV_HOR_RES_MAX   135
#define LV_VER_RES_MAX   240
#define LV_DISP_DEF_REFR_PERIOD  30

/*====================
 * DRAW SETTINGS
 *====================*/
#define LV_USE_DRAW_SW     1
#define LV_DRAW_SW_COMPLEX 0   /* HUGE flash saver */

/*====================
 * FONT SETTINGS
 *====================*/
/* Only keep ONE real font to save FLASH */
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Disable large font engine */
#define LV_USE_FONT_COMPRESSED 1
#define LV_FONT_FMT_TXT_LARGE 0

/* Font aliases so EEZ-generated UI compiles without adding new fonts */
#define lv_font_montserrat_12 lv_font_montserrat_14
#define lv_font_montserrat_16 lv_font_montserrat_14
#define lv_font_montserrat_24 lv_font_montserrat_14
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_30 1
#define LV_FONT_MONTSERRAT_30 1
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_20 1
/*====================
 * WIDGET SETTINGS
 *====================*/

#define LV_USE_LABEL      1
#define LV_USE_BTN        1
#define LV_USE_IMG        1

/* Turn OFF everything else to save FLASH */
#define LV_USE_ARC        0
#define LV_USE_ANIMIMG    0
#define LV_USE_BAR        0
#define LV_USE_BTNMATRIX  0   /* IMPORTANT: turn this OFF */
#define LV_USE_CANVAS     0
#define LV_USE_CHART      0
#define LV_USE_DROPDOWN   0
#define LV_USE_LINE       0
#define LV_USE_ROLLER     0
#define LV_USE_SLIDER     0
#define LV_USE_SWITCH     0
#define LV_USE_TABLE      0
#define LV_USE_TEXTAREA   0
#define LV_USE_KEYBOARD   0
#define LV_USE_MSGBOX     0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0

/*====================
 * IMAGE SETTINGS
 *====================*/
#define LV_USE_IMG_TRANSFORM 0   /* saves a LOT of flash */
#define LV_IMG_CACHE_DEF_SIZE 1

/* Disable external decoders */
#define LV_USE_PNG  0
#define LV_USE_BMP  0
#define LV_USE_SJPG 0

/* Disable special image color formats unless required */
#define LV_USE_IMG_CF_INDEXED 0
#define LV_USE_IMG_CF_ALPHA   0

/*====================
 * LOG SETTINGS
 *====================*/
#define LV_USE_LOG 0

#endif /* LV_CONF_H_ */


/* ===== Font Aliases to Save FLASH ===== */
#define lv_font_montserrat_26 lv_font_montserrat_14

/*==================
 * EXTRA COMPONENTS
 *==================*/

#define LV_USE_CALENDAR 0
#define LV_USE_CALENDAR_HEADER_ARROW 0
#define LV_USE_CALENDAR_HEADER_DROPDOWN 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_LIST 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

