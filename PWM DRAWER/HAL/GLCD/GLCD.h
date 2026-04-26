#ifndef GLCD_H_
#define GLCD_H_

#include "../../Service/std_types.h"

void GLCD_Init(void);
void GLCD_Clear(void);
void GLCD_Update(void);
void GLCD_SetPixel(u8 x, u8 y, u8 color);
void GLCD_DrawLine(u8 x1, u8 y1, u8 x2, u8 y2, u8 color);
void GLCD_ClearPages(u8 startPage, u8 endPage);   // NEW
void GLCD_PutChar(u8 page, u8 col, char ch, u8 color);
void GLCD_WriteString(u8 page, u8 col, const char *str, u8 color);

#endif
