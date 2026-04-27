#include "Service/std_types.h"
#include "Service/Bit_math.h"
#include "MCAL/DIO/DIO.h"
#include "MCAL/reg_def.h"
#include "HAL/GLCD/glcd.h"
#include <avr/interrupt.h>
#include <util/delay.h>
#include "MCAL/GIE/GIE.h"


#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#define MODE_INTERNAL  1
#define MODE_EXTERNAL  2
#define SYS_MODE       MODE_INTERNAL

#define UPDATE_DIVIDER   8

/* ============================================================
 *  Sampling Timer (Timer2 CTC)
 *  Prescaler /1, OCR2=499 → 32 000 Hz
 *  128 samples → 4 ms window → ~4 cycles of 976 Hz
 * ============================================================ */
#define SAMPLE_SIZE   128
u8       sample_buffer[SAMPLE_SIZE];
volatile u8 sample_index = 0;
volatile u8 buffer_full  = 0;

/* ============================================================
 *  External measurement state
 * ============================================================ */
#if SYS_MODE == MODE_EXTERNAL

volatile u8  edge_state       = 0;
volatile u8  measurement_done = 0;
volatile u32 period_ticks = 0;
volatile u32 high_ticks   = 0;

volatile u16 no_signal_timer = 0;
volatile u8  signal_valid    = 0;

volatile u16 t1_ovf = 0;

ISR(TIMER1_OVF_vect)
{
    t1_ovf++;
}

ISR(TIMER1_CAPT_vect)
{
    u16 icr = ICR1;
    u16 ovf = t1_ovf;
    if ((TIFR & (1 << TOV1)) && (icr < 0x8000))
        ovf++;

    u32 now = ((u32)ovf << 16) | (u32)icr;

    static u32 ts_rise1 = 0;
    static u32 ts_fall  = 0;

    switch (edge_state)
    {
        case 0:
            ts_rise1 = now;
            TCCR1B &= ~(1 << ICES1);
            edge_state = 1;
            break;
        case 1:
            ts_fall = now;
            TCCR1B |= (1 << ICES1);
            edge_state = 2;
            break;
        case 2:
        {
            u32 period = now    - ts_rise1;
            u32 high   = ts_fall - ts_rise1;

            if (period >= 20 && period <= 40000 && high < period)
            {
                period_ticks     = period;
                high_ticks       = high;
                measurement_done = 1;
                signal_valid     = 1;
                no_signal_timer  = 0;
            }

            ts_rise1 = now;
            TCCR1B &= ~(1 << ICES1);
            edge_state = 1;
            break;
        }
        default:
            edge_state = 0;
            TCCR1B |= (1 << ICES1);
            break;
    }
}
#endif  /* MODE_EXTERNAL */

ISR(TIMER2_COMP_vect)
{
    if (sample_index < SAMPLE_SIZE) {
#if SYS_MODE == MODE_INTERNAL
        u8 val = (PINB >> 3) & 1;
#else
        u8 val = (PIND >> 6) & 1;
#endif
        sample_buffer[sample_index++] = val;
        if (sample_index >= SAMPLE_SIZE) buffer_full = 1;
    }
}

/* ============================================================
 *  String helpers
 * ============================================================ */
static void TrimLeadingZeros(char *str)
{
    if (!str || !*str) return;
    char *s = str;
    while (*s == '0' && *(s+1) && *(s+1) != '.' && *(s+1) != ' ')
        s++;
    if (s != str) {
        char *d = str;
        while (*s) *d++ = *s++;
        *d = '\0';
    }
}

static void IntToStr(u32 val, char *str, u8 digits)
{
    s8 i = (s8)(digits - 1);
    for (; i >= 0; i--) {
        str[i] = (char)('0' + val % 10);
        val /= 10;
    }
    str[digits] = '\0';
}

/* ============================================================
 *  DrawWaveform
 * ============================================================ */
#define HIGH_Y  32
#define LOW_Y   60

static void DrawWaveform(void)
{
    GLCD_ClearPages(2, 7);

    u8 prev  = sample_buffer[0];
    u8 seg_x = 0;

    for (u8 i = 1; i < SAMPLE_SIZE; i++) {
        if (sample_buffer[i] != prev) {
            u8 y = (prev == 1) ? HIGH_Y : LOW_Y;
            for (s8 dy = -1; dy <= 1; dy++)
                GLCD_DrawLine(seg_x, (u8)(y+dy), i-1, (u8)(y+dy), 1);

            u8 y0 = (prev == 1) ? HIGH_Y : LOW_Y;
            u8 y1 = (prev == 0) ? HIGH_Y : LOW_Y;
            GLCD_DrawLine(i,   y0, i,   y1, 1);
            GLCD_DrawLine(i+1, y0, i+1, y1, 1);

            seg_x = i;
            prev  = sample_buffer[i];
        }
    }
    u8 y = (prev == 1) ? HIGH_Y : LOW_Y;
    for (s8 dy = -1; dy <= 1; dy++)
        GLCD_DrawLine(seg_x, (u8)(y+dy), SAMPLE_SIZE-1, (u8)(y+dy), 1);
}

/* ============================================================
 *  UpdateText
 * ============================================================ */
static void UpdateText(u32 freq_hz, u8 duty_pct, u32 period_us, u8 present)
{
    char str[16];
    GLCD_ClearPages(0, 1);

    if (!present) {
        GLCD_WriteString(0, 0, "No signal", 1);
        return;
    }

    u32 khz_x100 = freq_hz / 10UL;
    u16 khz_int  = (u16)(khz_x100 / 100UL);
    u8  khz_frac = (u8) (khz_x100 % 100UL);

    IntToStr(khz_int, str, 3);
    str[3] = '.';
    if (khz_frac < 10) { str[4]='0'; IntToStr(khz_frac, str+5, 1); str[6]=' '; }
    else               {              IntToStr(khz_frac, str+4, 2); str[6]=' '; }
    str[7]='K'; str[8]='H'; str[9]='z'; str[10]='\0';
    TrimLeadingZeros(str);
    GLCD_WriteString(0,  0, "F:",  1);
    GLCD_WriteString(0, 14, str,   1);

    IntToStr(duty_pct, str, 3);
    str[3]=' '; str[4]='%'; str[5]='\0';
    TrimLeadingZeros(str);
    GLCD_WriteString(0, 78, "DC:", 1);
    GLCD_WriteString(0, 98, str,   1);

    if (period_us >= 1000) {
        u16 ms_int  = (u16)(period_us / 1000UL);
        u8  ms_frac = (u8)((period_us % 1000UL) / 10UL);
        IntToStr(ms_int, str, 3);
        str[3] = '.';
        if (ms_frac < 10) { str[4]='0'; IntToStr(ms_frac, str+5, 1); str[6]=' '; str[7]='m'; str[8]='s'; str[9]='\0'; }
        else              {              IntToStr(ms_frac, str+4, 2); str[6]=' '; str[7]='m'; str[8]='s'; str[9]='\0'; }
    } else {
        IntToStr(period_us, str, 4);
        str[4]=' '; str[5]='u'; str[6]='s'; str[7]='\0';
    }
    TrimLeadingZeros(str);
    GLCD_WriteString(1,  0, "T:", 1);
    GLCD_WriteString(1, 14, str,  1);
}

/* ============================================================
 *  Hardware init
 * ============================================================ */
static void InitInternalPWM(void)
{
    DIO_voidSetPinDirection(DIO_PORTB, DIO_PIN_3, DIO_PIN_OUTPUT);
    TCCR0 = (1<<WGM00)|(1<<WGM01)|(1<<COM01)|(1<<CS01)|(1<<CS00);
    OCR0  = 127;
}

static void InitExternalMeasure(void)
{
    DIO_voidSetPinDirection(DIO_PORTD, DIO_PIN_6, DIO_INPUT);
    DIO_voidSetPinValue(DIO_PORTD, DIO_PIN_6, DIO_PIN_LOW);
    TCCR1A = 0x00;
    TCCR1B = (1<<ICNC1)|(1<<ICES1)|(1<<CS11);
    TIMSK |= (1<<TICIE1)|(1<<TOIE1);
}

static void InitSamplingTimer(void)
{
    TCCR2 = (1<<WGM21) | (1<<CS20);
    OCR2  = 499;
    TIMSK |= (1<<OCIE2);
}


int main(void)
{
    /* ── Welcome sequence ── */
    GLCD_Init();
    GLCD_Clear();
    _delay_ms(100);

    GLCD_WriteString(3, 20, "Initializing...", 1);
    GLCD_Update();
    _delay_ms(2000);

    GLCD_Clear();
#if SYS_MODE == MODE_INTERNAL
    GLCD_WriteString(3, 20, "Internal Mode", 1);
#else
    GLCD_WriteString(3, 20, "External Mode", 1);
#endif
    GLCD_Update();
    _delay_ms(1000);

    GLCD_Clear();



    GIE_voidEnableGlobalInterrupt();

#if SYS_MODE == MODE_INTERNAL
    InitInternalPWM();
#else
    InitExternalMeasure();
#endif
    InitSamplingTimer();

#if SYS_MODE == MODE_INTERNAL
    const u32 freq_hz_int   = (F_CPU / 64UL) / 256UL;
    const u8  duty_int      = (u8)(((u32)(OCR0 + 1) * 100UL) / 256UL);
    const u32 period_us_int = 1000000UL / freq_hz_int;
#endif

#if SYS_MODE == MODE_EXTERNAL
    #define AVG_N  8
    u32 acc_period = 0;
    u32 acc_high   = 0;
    u8  acc_count  = 0;
    u32 last_freq   = 0;
    u8  last_duty   = 0;
    u32 last_period = 0;
#endif

    u8 upd = 0;

    while (1)
    {
        if (buffer_full)
        {
            buffer_full  = 0;
            sample_index = 0;

#if SYS_MODE == MODE_EXTERNAL
            if (measurement_done)
            {
                measurement_done = 0;
                GIE_voidDisableGlobalInterrupt();
                u32 pt = period_ticks;
                u32 ht = high_ticks;
                GIE_voidEnableGlobalInterrupt();
                acc_period += pt;
                acc_high   += ht;
                acc_count++;
            }
            if (no_signal_timer < 200) no_signal_timer++;
#endif

            if (++upd >= UPDATE_DIVIDER)
            {
                upd = 0;
                DrawWaveform();

#if SYS_MODE == MODE_INTERNAL
                UpdateText(freq_hz_int, duty_int, period_us_int, 1);
#else
                if (acc_count >= AVG_N)
                {
                    u32 avg_period = acc_period / acc_count;
                    u32 avg_high   = acc_high   / acc_count;
                    acc_period = 0;
                    acc_high   = 0;
                    acc_count  = 0;
                    no_signal_timer = 0;
                    signal_valid    = 1;

                    u32 period_us = avg_period / 2UL;
                    if (period_us == 0) period_us = 1;
                    u32 freq_hz   = 2000000UL / avg_period;
                    u8  duty      = (u8)((avg_high * 100UL) / avg_period);

                    u32 freq_diff = (freq_hz > last_freq)
                                    ? (freq_hz - last_freq)
                                    : (last_freq - freq_hz);
                    u8 changed = (freq_diff > (last_freq / 100UL + 1))
                               || (duty != last_duty);

                    if (changed || last_freq == 0) {
                        last_freq   = freq_hz;
                        last_duty   = duty;
                        last_period = period_us;
                        UpdateText(freq_hz, duty, period_us, 1);
                    } else {
                        UpdateText(last_freq, last_duty, last_period, 1);
                    }
                }
                else if (signal_valid == 0 || no_signal_timer >= 200)
                {
                    acc_period = 0; acc_high = 0; acc_count = 0;
                    signal_valid = 0;
                    last_freq = 0;
                    UpdateText(0, 0, 0, 0);
                }
#endif
                GLCD_Update();
            }
        }
    }
    return 0;
}
