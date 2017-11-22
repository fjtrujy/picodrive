# vim:filetype=mips

# Some misc routines for MIPS R5900.

.set noreorder

.text
.align 4

#I don't know about the PSP, but the PS2's EE memcpy and memset functions are optimized to support its 64-bit architecture. Perhaps the existence of this file suggests that the PSPSDK doesn't have such a thing?
.extern memset
.extern memcpy

.globl memset32 # int *dest, int c, int count

memset32:
	j memset
	sll $a2, $a2, 2

.globl memset32_uncached # int *dest, int c, int count

memset32_uncached:
	j memset
	sll $a2, $a2, 2

.globl memcpy32 # int *dest, int *src, int count

memcpy32:
	j memcpy
	sll $a2, $a2, 2

