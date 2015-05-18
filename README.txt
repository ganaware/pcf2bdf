
				pcf2bdf



INSTALL

	for gcc:
	make -f Makefile.gcc install

	for visual c++:
	nmake -f Makefile.vc

NAME
	pcf2bdf  - convert X font from Portable Compiled Format to
	Bitmap Distribution Format

SYNOPSIS
	pcf2bdf [ -v ] [ -o outputfile ] [ fontfile.pcf[.gz] ]

DESCRIPTION
	Pcf2bdf is a font de-compiler.  It converts  X  font  from
	Portable Compiled Format (PCF) to Bitmap Distribution For-
	mat (BDF).  It can also accept  a  compressed/gzipped  PCF
	file as input, but gzip must be found in your PATH.

	FONTBOUNDINGBOX in a BDF file is not used by bdftopcf , so
	pcf2bdf generates irresponsible values.

OPTIONS
	-v      very verbose output.

	-o output-file-name
		 By default pcf2bdf writes the bdf file to standard
		 output; this option gives the name of a file to be
		 used instead.

SEE ALSO
	bdftopcf(1), X(7)

COPYRIGHT
	Copyright (c) 2002, 2015, TAGA Nayuta <nayuta@ganaware.jp>
	
	Permission is hereby granted, free of charge, to any person
	obtaining a copy of this software and associated documentation
	files (the "Software"), to deal in the Software without
	restriction, including without limitation the rights to use,
	copy, modify, merge, publish, distribute, sublicense, and/or
	sell copies of the Software, and to permit persons to whom the
	Software is furnished to do so, subject to the following
	conditions:
	
	The above copyright notice and this permission notice shall be
	included in all copies or substantial portions of the
	Software.
	
	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
	KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
	WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
	PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
	COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
	OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
	SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

HISTORY

	1.05 2015/05/18

	- The size should come from POINT_SIZE instead of PIXEL_SIZE.
	- Internal quotation characters in a string property should be
	  indicated (or "quoted”) by using two quotation characters
	  in a row.
	- Fix avoid crash with large compressed metrics tables.
	  (Colin Watson)
	- Fix support gzipped source.
	  (Colin Watson)
	- Fix use C++ compiler and simplify using GNU extensions.
	  (Jonas Smedegaard)
	- other fixes.

	1.04 2002/10/21
	1.03 1999/03/01
	1.02 1998/04/27
	1.01 1998/03/21
	1.00 1998/03/16
