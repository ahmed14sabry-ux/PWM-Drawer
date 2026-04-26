#ifndef GLCD_CFG_H_
#define GLCD_CFG_H_

/* ---------- Data port ---------- */
#define GLCD_DATA_PORT      PORTA

/* ---------- RS, RW, E (PORTB) ---------- */
#define GLCD_CTRL_PORT_RS_RW_E   PORTB
#define GLCD_RS_PIN         0
#define GLCD_RW_PIN         1
#define GLCD_EN_PIN         2

/* ---------- CS1, CS2, RST (PORTC) ---------- */
#define GLCD_CTRL_PORT_CS_RST    PORTC
#define GLCD_CS1_PIN        0
#define GLCD_CS2_PIN        1
#define GLCD_RST_PIN        2

/* ---------- LCD dimensions ---------- */
#define GLCD_WIDTH          128
#define GLCD_HEIGHT         64
#define GLCD_PAGES          8       /* 64 / 8 */
#define GLCD_CHIP_WIDTH     64

#endif
