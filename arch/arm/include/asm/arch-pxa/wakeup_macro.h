/*
 * This macro tests the CPU reset state and takes appropriate actions as follows:
 *
 *   Watchdog Reset: (Reset from linux)
 *      Continue to Normal boot
 *   Sleep-Exit Reset (Sleep, or Deep-Sleep from linux poweroff() / other boot):
 *      Check for non-zero in Scratch register, if so resume from Scratch register 
 *      If not issue WatchDog reset
 *   All other reset types:
 *      Enter Deep-Sleep mode
 * Clobbered regs: r4, r5
 */

.macro	z2_wakeup
	ldr		r4,		=RCSR
	ldr		r5,		[r4]
	and		r5,		r5,		#(RCSR_GPR | RCSR_SMR | RCSR_WDR | RCSR_HWR)
	str		r5,		[r4]

// check for Watchdog Reset
	teq		r5,		#RCSR_WDR
	beq		z2_wakeup_exit

// check for Sleep-Exit Reset
	teq		r5,		#RCSR_SMR
	beq		z2_wakeup_sleep_exit

// Nope something else woke us up
// zero the scratch register
	ldr		r4,		=PSPR
	mov		r5,		#0
	str		r5,		[r4]
// enter deep sleep
	mov		r4,		#7
	mcr		p14,	0, r4, c7, c0, 0

z2_wakeup_sleep_loop:
	b			z2_wakeup_sleep_loop

z2_wakeup_sleep_exit:
	ldr		r4,		=PSSR
	mov		r5,		#PSSR_PH
	str		r5,		[r4]

	ldr		r4,		=PSPR
	ldr		r5,		[r4]
	cmp		r5,		#0x0
	beq		z2_wakeup_watchdog_reset
// continue wakeup
	ldr		pc,		[r4]

// scratch == 0 (exit from deep sleep) so perform a clean watchdog reset ( when control returns, initiate a boot )
z2_wakeup_watchdog_reset:

// enable Watchdog match to generate reset
	ldr		r4,		=OWER
	mov		r5,		#OWER_WME
	str		r5,		[r4]
	
// Clear the Match Status for Watchdog: M3

	ldr		r4,		=OSSR
	mov		r5,		#OSSR_M3
	str		r5,		[r4]

// reset when OSMR3 and OSCR match ~ 100 ms
	ldr		r4,		=OSCR
	ldr		r5,		[r4]
	add		r5,		r5,		#0x50000
	ldr		r4,		=OSMR3
	str		r5,		[r4]

	b			z2_wakeup_sleep_loop

z2_wakeup_exit:
.endm