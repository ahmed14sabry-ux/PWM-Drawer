#include "../../Service/std_types.h"
#include "../../Service/Bit_math.h"
#include "../../MCAL/DIO/DIO.h"
#include "glcd.h"
#include "glcd_cfg.h"
#include <util/delay.h>

/* 128x64 framebuffer */
static u8 glcd_buffer[GLCD_PAGES][GLCD_WIDTH];

/* Chip select - CS1/CS2 on PORTC */
static void GLCD_SelectChip(u8 chip) {
    switch (chip) {
        case 1:
            DIO_voidSetPinValue(DIO_PORTC, GLCD_CS1_PIN, DIO_PIN_HIGH);
            DIO_voidSetPinValue(DIO_PORTC, GLCD_CS2_PIN, DIO_PIN_LOW);
            break;
        case 2:
            DIO_voidSetPinValue(DIO_PORTC, GLCD_CS1_PIN, DIO_PIN_LOW);
            DIO_voidSetPinValue(DIO_PORTC, GLCD_CS2_PIN, DIO_PIN_HIGH);
            break;
        default:
            DIO_voidSetPinValue(DIO_PORTC, GLCD_CS1_PIN, DIO_PIN_LOW);
            DIO_voidSetPinValue(DIO_PORTC, GLCD_CS2_PIN, DIO_PIN_LOW);
            break;
    }
}

/* Enable pulse with proper timing (min 450 ns) */
static void GLCD_EnablePulse(void) {
    DIO_voidSetPinValue(DIO_PORTB, GLCD_EN_PIN, DIO_PIN_HIGH);
    _delay_us(1);          // ~1 µs high time – well above 450 ns
    DIO_voidSetPinValue(DIO_PORTB, GLCD_EN_PIN, DIO_PIN_LOW);
    _delay_us(1);          // recovery time
}

/* Write command or data – RS/RW on PORTB, data on PORTA */
static void GLCD_WriteCmdData(u8 data, u8 rs) {
    DIO_voidSetPinValue(DIO_PORTB, GLCD_RS_PIN, rs);
    DIO_voidSetPinValue(DIO_PORTB, GLCD_RW_PIN, DIO_PIN_LOW);
    DIO_voidSetPortValue(DIO_PORTA, data);
    GLCD_EnablePulse();
}

static void GLCD_WriteCommand(u8 cmd, u8 chip) {
    GLCD_SelectChip(chip);
    GLCD_WriteCmdData(cmd, 0);
}

static void GLCD_WriteData(u8 data, u8 chip) {
    GLCD_SelectChip(chip);
    GLCD_WriteCmdData(data, 1);
}

/* Initialisation */
void GLCD_Init(void) {
    // RS,RW,E as outputs
    DIO_voidSetPinDirection(DIO_PORTB, GLCD_RS_PIN, DIO_PIN_OUTPUT);
    DIO_voidSetPinDirection(DIO_PORTB, GLCD_RW_PIN, DIO_PIN_OUTPUT);
    DIO_voidSetPinDirection(DIO_PORTB, GLCD_EN_PIN, DIO_PIN_OUTPUT);
    // CS1,CS2,RST as outputs
    DIO_voidSetPinDirection(DIO_PORTC, GLCD_CS1_PIN, DIO_PIN_OUTPUT);
    DIO_voidSetPinDirection(DIO_PORTC, GLCD_CS2_PIN, DIO_PIN_OUTPUT);
    DIO_voidSetPinDirection(DIO_PORTC, GLCD_RST_PIN, DIO_PIN_OUTPUT);
    // Data port output
    DIO_voidSetPortDirection(DIO_PORTA, DIO_PORT_OUTPUT);

    // Reset sequence
    DIO_voidSetPinValue(DIO_PORTC, GLCD_RST_PIN, DIO_PIN_LOW);
    _delay_us(100);
    DIO_voidSetPinValue(DIO_PORTC, GLCD_RST_PIN, DIO_PIN_HIGH);

    // Display ON (0x3F), set start line 0 (0xC0) for both chips
    GLCD_WriteCommand(0x3F, 1);
    GLCD_WriteCommand(0x3F, 2);
    GLCD_WriteCommand(0xC0, 1);
    GLCD_WriteCommand(0xC0, 2);

    GLCD_Clear();
}

void GLCD_Clear(void) {
    for (u8 page = 0; page < GLCD_PAGES; page++)
        for (u8 col = 0; col < GLCD_WIDTH; col++)
            glcd_buffer[page][col] = 0x00;
    GLCD_Update();
}

void GLCD_ClearPages(u8 startPage, u8 endPage) {
    if (startPage >= GLCD_PAGES || endPage >= GLCD_PAGES) return;
    for (u8 page = startPage; page <= endPage; page++)
        for (u8 col = 0; col < GLCD_WIDTH; col++)
            glcd_buffer[page][col] = 0x00;
}

void GLCD_Update(void) {
    u8 page, col;
    for (page = 0; page < GLCD_PAGES; page++) {
        GLCD_WriteCommand(0xB8 | page, 1);   // set page
        GLCD_WriteCommand(0xB8 | page, 2);
        GLCD_WriteCommand(0x40, 1);          // set column 0
        GLCD_WriteCommand(0x40, 2);

        for (col = 0; col < GLCD_CHIP_WIDTH; col++) {
            GLCD_WriteData(glcd_buffer[page][col], 1);
            GLCD_WriteData(glcd_buffer[page][col + GLCD_CHIP_WIDTH], 2);
        }
    }
}

void GLCD_SetPixel(u8 x, u8 y, u8 color) {
    if (x >= GLCD_WIDTH || y >= GLCD_HEIGHT) return;
    u8 page = y / 8;
    u8 bit  = y % 8;
    if (color)
        glcd_buffer[page][x] |= (1 << bit);
    else
        glcd_buffer[page][x] &= ~(1 << bit);
}

/* Bresenham line drawing  */
void GLCD_DrawLine(u8 x1, u8 y1, u8 x2, u8 y2, u8 color) {
    s16 dx = (s16)x2 - (s16)x1;
    s16 dy = (s16)y2 - (s16)y1;
    s16 stepx = (dx > 0) ? 1 : -1;
    s16 stepy = (dy > 0) ? 1 : -1;
    dx = (dx > 0) ? dx : -dx;
    dy = (dy > 0) ? dy : -dy;
    s16 err = dx - dy;
    while (1) {
        GLCD_SetPixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        s16 e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += stepx; }
        if (e2 <  dx) { err += dx; y1 += stepy; }
    }
}


static const u8 font_idx[128] = {
    [' ']=0, ['.']=1, ['%']=2, [':']=3,
    ['0']=4, ['1']=5, ['2']=6, ['3']=7,
    ['4']=8, ['5']=9, ['6']=10, ['7']=11,
    ['8']=12, ['9']=13,
    ['C']=14, ['D']=15, ['E']=16, ['F']=17,
    ['H']=18, ['I']=19, ['K']=20, ['M']=21,
    ['T']=22,
    ['a']=23, ['d']=24, ['e']=25, ['g']=26,
    ['i']=27, ['l']=28, ['m']=29, ['n']=30,
    ['o']=31, ['q']=32, ['r']=33, ['s']=34,
    ['t']=35, ['u']=36, ['x']=37, ['y']=38,
    ['z']=39,
    ['c']=14, ['f']=17, ['h']=18, ['k']=20,
    ['b']=14, ['j']=19, ['p']=14,
    ['v']=14, ['w']=14, ['r']=33,
};

static const u8 font5x7[][5] = {
    // 0: space
    {0x00,0x00,0x00,0x00,0x00},
    // 1: .
    {0x00,0x00,0x60,0x60,0x00},
    // 2: %
    {0x44,0x08,0x10,0x20,0x44},
    // 3: :
    {0x00,0x00,0x12,0x00,0x00},
    // 4: 0
    {0x3E,0x51,0x49,0x45,0x3E},
    // 5: 1
    {0x00,0x42,0x7F,0x40,0x00},
    // 6: 2
    {0x62,0x51,0x49,0x49,0x46},
    // 7: 3
    {0x22,0x41,0x49,0x49,0x36},
    // 8: 4
    {0x18,0x14,0x12,0x7F,0x10},
    // 9: 5
    {0x2F,0x49,0x49,0x49,0x31},
    // 10: 6
    {0x3E,0x49,0x49,0x49,0x32},
    // 11: 7
    {0x01,0x71,0x09,0x05,0x03},
    // 12: 8
    {0x36,0x49,0x49,0x49,0x36},
    // 13: 9
    {0x26,0x49,0x49,0x49,0x3E},
    // 14: C
    {0x3E,0x41,0x40,0x41,0x3E},
    // 15: D
    {0x7F,0x41,0x41,0x22,0x1C},
    // 16: E
    {0x7F,0x49,0x49,0x49,0x41},
    // 17: F
    {0x7F,0x09,0x09,0x09,0x01},
    // 18: H
    {0x7F,0x08,0x08,0x08,0x7F},
    // 19: I
    {0x41,0x41,0x7F,0x41,0x41},
    // 20: K
    {0x7F,0x08,0x14,0x22,0x41},
    // 21: M
    {0x7F,0x02,0x04,0x02,0x7F},
    // 22: T
    {0x01,0x01,0x7F,0x01,0x01},
    // 23: a
    {0x20,0x54,0x54,0x54,0x78},
    // 24: d
    {0x38,0x44,0x44,0x24,0x7F},
    // 25: e
    {0x38,0x54,0x54,0x54,0x18},
    // 26: g
    {0x08,0x54,0x54,0x54,0x38},
    // 27: i
    {0x00,0x44,0x7D,0x40,0x00},
    // 28: l
    {0x41,0x41,0x7F,0x41,0x41},
    // 29: m
    {0x7C,0x04,0x38,0x04,0x7C},
    // 30: n
    {0x7C,0x08,0x04,0x04,0x78},
    // 31: o
    {0x38,0x44,0x44,0x44,0x38},
    // 32: q
    {0x38,0x44,0x44,0x3C,0x04},
    // 33: r
    {0x7C,0x08,0x04,0x04,0x08},
    // 34: s
    {0x48,0x54,0x54,0x54,0x24},
    // 35: t
    {0x04,0x3E,0x44,0x44,0x20},
    // 36: u
    {0x3C,0x40,0x40,0x20,0x7C},
    // 37: x
    {0x44,0x28,0x10,0x28,0x44},
    // 38: y
    {0x1C,0x20,0x40,0x20,0x1C},
    // 39: z
    {0x44,0x64,0x54,0x4C,0x44}
};
void GLCD_PutChar(u8 page, u8 col, char ch, u8 color) {
    u8 idx = font_idx[(u8)ch];
    const u8 *bitmap = font5x7[idx];
    for (u8 i = 0; i < 5; i++) {
        u8 data = bitmap[i];
        for (u8 j = 0; j < 8; j++) {
            if (data & (1 << j))
                GLCD_SetPixel(col + i, page * 8 + j, color);
            else
                GLCD_SetPixel(col + i, page * 8 + j, !color);
        }
    }
}

void GLCD_WriteString(u8 page, u8 col, const char *str, u8 color) {
    while (*str) {
        GLCD_PutChar(page, col, *str++, color);
        col += 6;
        if (col > GLCD_WIDTH - 5) break;
    }
}
