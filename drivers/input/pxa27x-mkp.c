/*
 * PXA27x matrix keypad controller driver
 *
 * Copyright (C) 2010 Marek Vasut <marek.vasut@gmail.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <common.h>
#include <stdio_dev.h>
#include <asm/arch/pxa-regs.h>

#define	DEVNAME		"pxa27x-mkp"
#define	KPASMKP(x)	__REG(0x41500028 + ((x) << 3))

struct {
	char	row;
	char	col;
	char	key;
	char	shift;
	char	alt;
	char	ctrl;
} keymap[] = {
	CONFIG_PXA27X_MKP_KEYMAP,
};

static unsigned char queue[64] = {0};
static int queue_len;

// autorepeat stuff
static unsigned char last_key = 0xff;
static char key_counter = 0;
// number of key scans before autorepeat kicks in
#define	KEY_REPEAT_FIRST	12
#define	KEY_REPEAT_NEXT		2

enum {
	MOD_NONE,
	MOD_SHIFT,
	MOD_ALT,
	MOD_CTRL,
};

static int kbd_get_mdf(int row, int col)
{
	char mod_shift[2] = CONFIG_PXA27X_MKP_MOD_SHIFT;
	char mod_alt[2] = CONFIG_PXA27X_MKP_MOD_ALT;
	char mod_ctrl[2] = CONFIG_PXA27X_MKP_MOD_CTRL;

	if (mod_shift[0] == row && mod_shift[1] == col)
		return MOD_SHIFT;
	if (mod_alt[0] == row && mod_alt[1] == col)
		return MOD_ALT;
	if (mod_ctrl[0] == row && mod_ctrl[1] == col)
		return MOD_CTRL;
	return MOD_NONE;
}

static void kbd_lookup(int row, int col, int mod)
{
	int i = 0;

	while (!(keymap[i].col == 0xff && keymap[i].row == 0xff)) {
		if (keymap[i].row == row && keymap[i].col == col) {
			static char key = 0xff;
			switch (mod) {
			case MOD_NONE:
				key = keymap[i].key;
				break;
			case MOD_SHIFT:
				key = keymap[i].shift;
				break;
			case MOD_ALT:
				key = keymap[i].alt;
				break;
			case MOD_CTRL:
				key = keymap[i].ctrl;
				break;
			}
			if ( key != 0xff) {
				if (key != last_key) {
					queue[queue_len++]=key; last_key=key; key_counter=0;
				} else // same key as before
					if (key_counter<KEY_REPEAT_FIRST) key_counter++; //ignore key press
					else { // ok, autorepeat
						queue[queue_len++]=key;
						key_counter = (KEY_REPEAT_FIRST-KEY_REPEAT_NEXT);
					} 
			}
		}
		i++;
	}
}

static void kbd_read(void)
{
	uint32_t reg;
	int col, row;
	int modif = 0;
	int mod = MOD_NONE;
	int numkeys;
	KPC |= KPC_AS; // start one automatic scan
	while (KPC & KPC_AS); // wait for scan to finish

	numkeys = (KPAS>>26)&0x1f;
	if (numkeys == 0) { // no key pressed, clear autorepeat counter
		last_key=0xFF; key_counter=0;
	} else if (numkeys == 1) { // exactly one key pressed
		reg = KPAS&0xFF; col=reg&0x0F; row=reg>>4;
		if (kbd_get_mdf(row, col) != MOD_NONE) { //modifier only
			// TODO handle sticky modifiers here
			last_key=0xFF; key_counter=0; // no real key, clear autorepeat counter
		} else {
			kbd_lookup(row, col, mod);
		}
	}  else { 
	//multiple keys pressed, check KPASMKPx registers
	for (modif = 1; modif >= 0; modif--) {
		for (col = 0; col < 8; col += 2) {
			while ((reg = KPASMKP(col >> 1)) & KPASMKPx_SO);
			for (row = 0; row < 8; row++) {
				if (reg & (1 << row)) {
					if (modif) {
						mod = kbd_get_mdf(row, col);
						if (mod != MOD_NONE)
							goto cont;
					} else
						kbd_lookup(row, col, mod);
				}
				if ((reg >> 16) & (1 << row)) {
					if (modif) {
						mod = kbd_get_mdf(row, col + 1);
						if (mod != MOD_NONE)
							goto cont;
					} else
						kbd_lookup(row, col + 1, mod);
				}
			}
		}
cont:		while(0) {};
	} //for
	}
}

static int kbd_getc(void)
{
	if(!queue_len) {
		kbd_read();
		udelay(CONFIG_PXA27X_MKP_DELAY);
	}

	if (queue_len)
		return queue[--queue_len];
	else
		return 0;
}

static int kbd_testc(void)
{
	if (!queue_len)
		kbd_read();
	return queue_len;
}

int drv_keyboard_init(void)
{
	int error = 0;
	struct stdio_dev kbddev;
	if (!keymap)
		return -1;

	queue_len = 0;

	KPC = (CONFIG_PXA27X_MKP_MKP_ROWS << 26) |
		(CONFIG_PXA27X_MKP_MKP_COLS << 23) |
		(0xff << 13) | KPC_ME;
	KPKDI = CONFIG_PXA27X_MKP_DEBOUNCE;

	memset (&kbddev, 0, sizeof(kbddev));
	strcpy(kbddev.name, DEVNAME);
	kbddev.flags =  DEV_FLAGS_INPUT | DEV_FLAGS_SYSTEM;
	kbddev.putc = NULL ;
	kbddev.puts = NULL ;
	kbddev.getc = kbd_getc ;
	kbddev.tstc = kbd_testc ;

	error = stdio_register (&kbddev);
	return error;
}
