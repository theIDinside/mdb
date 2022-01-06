#!/bin/bash

# The study material I need, while writing the ELF & DWARF related stuff

echo "Pages to open:"
echo "Introduction to the DWARF debugging format (version 4.0 and down)"
echo "DWARF 5.0 standards document"
echo "IBM documentation; Exploring the DWARF debug format information. Sort of niche, but can provide insights in some cases for our purposes"
echo "'Poster' of ELF; a 101 on the ELF format for Linux"
echo "ELF Format (Tool Interface Standards document)"
echo "DWARF Wikipedia"
echo "Linux ABI Draft; System V Application Binary Interface - Linux Extensions version 0.1, Nov 28 / 2018"

firefox https://dwarfstd.org/doc/Debugging%20using%20DWARF-2012.pdf \
	https://dwarfstd.org/doc/DWARF5.pdf \
	https://developer.ibm.com/articles/au-dwarf-debug-format/ \
	https://raw.githubusercontent.com/corkami/pics/master/binary/elf101/elf101-64.pdf \
	http://www.skyfree.org/linux/references/ELF_Format.pdf \
	https://en.wikipedia.org/wiki/DWARF \
	https://raw.githubusercontent.com/wiki/hjl-tools/linux-abi/linux-abi-draft.pdf &
