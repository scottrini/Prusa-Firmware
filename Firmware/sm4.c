//sm4.c - simple 4-axis stepper control

#include "sm4.h"
#include <avr/io.h>

#include "boards.h"
#define bool int8_t
#define false 0
#define true 1
#include "Configuration_prusa.h"

/**/

//direction signal pinout - MiniRambo
//#define X_DIR_PIN    48 //PL1
//#define Y_DIR_PIN    49 //PL0
//#define Z_DIR_PIN    47 //PL2
//#define E0_DIR_PIN   43 //PL6

//direction signal pinout - EinsyRambo
//#define X_DIR_PIN    49 //PL0
//#define Y_DIR_PIN    48 //PL1
//#define Z_DIR_PIN    47 //PL2
//#define E0_DIR_PIN   43 //PL6

//step signal pinout - common for all rambo boards
//#define X_STEP_PIN   37 //PC0
//#define Y_STEP_PIN   36 //PC1
//#define Z_STEP_PIN   35 //PC2
//#define E0_STEP_PIN  34 //PC3


sm4_update_pos_cb sm4_update_pos = 0;

uint8_t sm4_get_dir(uint8_t axis)
{
	switch (axis)
	{
#if ((MOTHERBOARD == 200) || (MOTHERBOARD == 203))
	case 0: return (PORTL & 2)?0:1;
	case 1: return (PORTL & 1)?0:1;
	case 2: return (PORTL & 4)?0:1;
	case 3: return (PORTL & 64)?1:0;
#else if ((MOTHERBOARD == 303) || (MOTHERBOARD == 304))
	case 0: return (PORTL & 1)?1:0;
	case 1: return (PORTL & 2)?0:1;
	case 2: return (PORTL & 4)?1:0;
	case 3: return (PORTL & 64)?0:1;
#endif
	}
	return 0;
}

void sm4_set_dir(uint8_t axis, uint8_t dir)
{
	switch (axis)
	{
#if ((MOTHERBOARD == 200) || (MOTHERBOARD == 203))
	case 0: if (!dir) PORTL |= 2; else PORTL &= ~2; break;
	case 1: if (!dir) PORTL |= 1; else PORTL &= ~1; break;
	case 2: if (!dir) PORTL |= 4; else PORTL &= ~4; break;
	case 3: if (dir) PORTL |= 64; else PORTL &= ~64; break;
#else if ((MOTHERBOARD == 303) || (MOTHERBOARD == 304))
	case 0: if (dir) PORTL |= 1; else PORTL &= ~1; break;
	case 1: if (!dir) PORTL |= 2; else PORTL &= ~2; break;
	case 2: if (dir) PORTL |= 4; else PORTL &= ~4; break;
	case 3: if (!dir) PORTL |= 64; else PORTL &= ~64; break;
#endif
	}
	asm("nop");
}

uint8_t sm4_get_dir_bits(void)
{
	uint8_t register dir_bits = 0;
	uint8_t register portL = PORTL;
	//TODO -optimize in asm
#if ((MOTHERBOARD == 200) || (MOTHERBOARD == 203))
	if (portL & 2) dir_bits |= 1;
	if (portL & 1) dir_bits |= 2;
	if (portL & 4) dir_bits |= 4;
	if (portL & 64) dir_bits |= 8;
	dir_bits ^= 0x07; //invert XYZ, do not invert E
#else if ((MOTHERBOARD == 303) || (MOTHERBOARD == 304))
	if (portL & 1) dir_bits |= 1;
	if (portL & 2) dir_bits |= 2;
	if (portL & 4) dir_bits |= 4;
	if (portL & 64) dir_bits |= 8;
	dir_bits ^= 0x0a; //invert YE, do not invert XZ
#endif
	return dir_bits;
}

void sm4_set_dir_bits(uint8_t dir_bits)
{
	uint8_t register portL = PORTL;
	portL &= 0xb8; //set direction bits to zero
	//TODO -optimize in asm
#if ((MOTHERBOARD == 200) || (MOTHERBOARD == 203))
	dir_bits ^= 0x07; //invert XYZ, do not invert E
	if (dir_bits & 1) portL |= 2;  //set X direction bit
	if (dir_bits & 2) portL |= 1;  //set Y direction bit
	if (dir_bits & 4) portL |= 4;  //set Z direction bit
	if (dir_bits & 8) portL |= 64; //set E direction bit
#else if ((MOTHERBOARD == 303) || (MOTHERBOARD == 304))
	dir_bits ^= 0x0a; //invert YE, do not invert XZ
	if (dir_bits & 1) portL |= 1;  //set X direction bit
	if (dir_bits & 2) portL |= 2;  //set Y direction bit
	if (dir_bits & 4) portL |= 4;  //set Z direction bit
	if (dir_bits & 8) portL |= 64; //set E direction bit
#endif
	PORTL = portL;
	asm("nop");
}

void sm4_do_step(uint8_t axes_mask)
{
#if ((MOTHERBOARD == 200) || (MOTHERBOARD == 203) || (MOTHERBOARD == 303) || (MOTHERBOARD == 304))
	uint8_t register portC = PORTC & 0xf0;
	PORTC = portC | (axes_mask & 0x0f); //set step signals by mask
	asm("nop");
	PORTC = portC; //set step signals to zero
	asm("nop");
#endif //((MOTHERBOARD == 200) || (MOTHERBOARD == 203) || (MOTHERBOARD == 303) || (MOTHERBOARD == 304))
}

int isqrt(int n)
{
	int a = 1;
	int b = n;
	while (abs(a - b) > 1)
	{
		b = n / a;
		a = (a + b) / 2;
	}
	return a;
}

uint8_t sm4_line_xyz_ui(uint16_t dx, uint16_t dy, uint16_t dz, uint16_t delay_us, sm4_stop_cb stop_cb)
{
	uint16_t dd = (uint16_t)(sqrt((float)(((uint32_t)dx)*dx + ((uint32_t)dy*dy) + ((uint32_t)dz*dz))) + 0.5);
	uint16_t nd = dd;
	uint16_t cx = dd;
	uint16_t cy = dd;
	uint16_t cz = dd;
	uint16_t x = 0;
	uint16_t y = 0;
	uint16_t z = 0;
	uint8_t stop = 0; 
	while ((nd--) && stop_cb && !(stop = (*stop_cb)()))
	{
		uint8_t sm = 0; //step mask
		if (cx <= dx)
		{
			sm |= 1;
			cx += dd;
			x++;
		}
		if (cy <= dy)
		{
			sm |= 2;
			cy += dd;
			y++;
		}
		if (cz <= dz)
		{
			sm |= 4;
			cz += dd;
			z++;
		}
		cx -= dx;
		cy -= dy;
		cz -= dz;
		sm4_do_step(sm);
		delayMicroseconds(delay_us);
	}
	if (sm4_update_pos) (*sm4_update_pos)(x, y, z, 0);
	return stop;
}

