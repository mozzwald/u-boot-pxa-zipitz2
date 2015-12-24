/*
 * (C) Copyright 2010
 * Texas Instruments, <www.ti.com>
 *
 * Author :
 *	Aneesh V	<aneesh@ti.com>
 *	Steve Sakoman	<steve@sakoman.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <common.h>
#include <asm/arch/sys_proto.h>

/*
 *  get_device_type(): tell if GP/HS/EMU/TST
 */
u32 get_device_type(void)
{
	return 0;
}

/*
 * get_board_rev() - get board revision
 */
u32 get_board_rev(void)
{
	return 0x20;
}

/*
 * Print CPU information
 */
int print_cpuinfo(void)
{

	puts("CPU  : OMAP4430\n");

	return 0;
}

