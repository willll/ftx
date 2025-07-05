#
# Makefile for Sega Saturn SD card and FAT filesystem access ROM code.
# From Sega Saturn USB flash cart ROM, by Anders Montonen, 2012
# Creative Commons Attribution-ShareAlike 3.0 Unported (CC BY-SA 3.0)
#

CC	=	saturn-sh2-elf-gcc
LD	=	saturn-sh2-elf-gcc
AS	=	saturn-sh2-elf-as

# Note: Added -g -Wa,-a,-ad in order to check assembly listings
FLAGS	=	-g -Wa,-a,-ad -Wall -Werror -m2 -O2
LDFLAGS	=	-nostartfiles -T bart.lnk
OBJ		=	obj/copystub.o
EXE		=	copystub.bin

all	:	$(EXE)

$(EXE)	:	$(OBJ)
		$(LD) $(LDFLAGS) $(OBJ) -o $(EXE)

obj/%.o :	%.c
		$(CC) -c $< -o $@ $(FLAGS) > $@.lst

obj/%.o :	%.S
		$(AS) $< -o $@

clean	:
		rm -f obj/*.*

mrproper	:
		rm -f obj/*.*
		rm -f $(EXE)

makedir :
		mkdir obj

#
# end of makefile
#
