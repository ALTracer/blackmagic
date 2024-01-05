/* Define memory regions. */
MEMORY
{
	rom (rx) : ORIGIN = 0x08000000, LENGTH =  256K
	ram (rwx) : ORIGIN = 0x20000000, LENGTH = 48K
}

/* Include the platform common linker script. */
INCLUDE ../common/blackmagic.ld
