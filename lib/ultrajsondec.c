/*
Copyright (c) 2011-2013, ESN Social Software AB and Jonas Tarnstrom
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
* Neither the name of the ESN Social Software AB nor the
names of its contributors may be used to endorse or promote products
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ESN SOCIAL SOFTWARE AB OR JONAS TARNSTROM BE LIABLE 
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


Portions of code from MODP_ASCII - Ascii transformations (upper/lower, etc)
http://code.google.com/p/stringencoders/
Copyright (c) 2007  Nick Galbreath -- nickg [at] modp [dot] com. All rights reserved.

Numeric decoder derived from from TCL library
http://www.opensource.apple.com/source/tcl/tcl-14/tcl/license.terms
* Copyright (c) 1988-1993 The Regents of the University of California.
* Copyright (c) 1994 Sun Microsystems, Inc.
*/

#include "ultrajson.h"
#include <math.h>
#include <assert.h>
#include <string.h>
#include <limits.h>
#include <wchar.h>

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#ifndef NULL
#define NULL 0
#endif

struct DecoderState
{
	char *start;
	char *end;
	wchar_t *escStart;
	wchar_t *escEnd;
	int escHeap;
	int lastType;
	JSONObjectDecoder *dec;
};

JSOBJ FASTCALL_MSVC decode_any( struct DecoderState *ds) FASTCALL_ATTR;
typedef JSOBJ (*PFN_DECODER)( struct DecoderState *ds);
#define RETURN_JSOBJ_NULLCHECK(_expr) return(_expr);


static JSOBJ SetError( struct DecoderState *ds, int offset, const char *message)
{
	ds->dec->errorOffset = ds->start + offset;
	ds->dec->errorStr = (char *) message;
	return NULL;
}

static void ClearError( struct DecoderState *ds)
{
	ds->dec->errorOffset = 0;
	ds->dec->errorStr = NULL;
}

FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_numeric (struct DecoderState *ds)
{
	static int maxExponent = 511;	/* Largest possible base 10 exponent.  Any
									* exponent larger than this will already
									* produce underflow or overflow, so there's
									* no need to worry about additional digits.
									*/
	static const double powersOf10[] = {	/* Table giving binary powers of 10.  Entry */
		10.,			/* is 10^2^i.  Used to convert decimal */
		100.,			/* exponents into floating-point numbers. */
		1.0e4,
		1.0e8,
		1.0e16,
		1.0e32,
		1.0e64,
		1.0e128,
		1.0e256
	};

	static const JSINT64 decPowerOf10[] = {
		1LL,
		10LL,
		100LL,
		1000LL,
		10000LL,
		100000LL,
		1000000LL,
		10000000LL,
		100000000LL,
		1000000000LL,
		10000000000LL,
		100000000000LL,
		1000000000000LL,
		10000000000000LL,
		100000000000000LL,
		1000000000000000LL,
		10000000000000000LL,
		100000000000000000LL,
		1000000000000000000LL,
		10000000000000000000LL,
	};

	int sign = FALSE;
	int expSign = FALSE;
	double fraction, dblExp;
	const double *d;
	const char *p;
	int expNeg = 1;
	int fracNeg = 1;
	int exp = 0;			/* Exponent read from "EX" field. */
	int fracExp = 0;		/* Exponent that derives from the fractional
							* part.  Under normal circumstatnces, it is
							* the negative of the number of digits in F.
							* However, if I is very long, the last digits
							* of I get dropped (otherwise a long I with a
							* large negative exponent could cause an
							* unnecessary overflow on I alone).  In this
							* case, fracExp is incremented one for each
							* dropped digit. */
	int mantSize;			/* Number of digits in mantissa. */
	int decPt = -1;			/* Number of mantissa digits BEFORE decimal
							* point. */
	JSUINT32 frac1;
	JSUINT64 frac2;
	JSUINT64 overflowLimit = 6854775807;

	/*
	* Strip off leading blanks and check for a sign.
	*/

	p = ds->start;
	if (*p == '-') 
	{
		sign = TRUE;
		p ++;
		fracNeg = -1;	
		overflowLimit = 6854775808;	
	} 
	else
	{
		if (*p == '+')
			p ++;
	}

	//=========================================================================
	// Fraction part 1 
	//=========================================================================
	frac1 = 0;
	frac2 = 0;
	mantSize = 0;
	while (mantSize < 9)
	{
		int chr = (int)(unsigned char) *p;

		switch (chr)
		{
		case '.':
			//FIXME: Test for more than one decimal point here
			decPt = mantSize;
			p ++;
			continue;

		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
			frac1 = 10U *frac1 + (chr - '0');
			p ++;
			mantSize ++;
			break;

		default: goto END_MANTISSA_LOOP; break;
		}
	}


	//=========================================================================
	// Fraction part 2 
	//=========================================================================
	frac2 = 0;
	while (mantSize < 19)
	{
		int chr = (int)(unsigned char) *p;

		switch (chr)
		{
		case '.':
			//FIXME: Test for more than one decimal point here
			decPt = mantSize;
			p ++;
			continue;

		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
			frac2 = 10ULL * frac2 + (chr - '0');
			p ++;
			mantSize ++;
			break;

		default: goto END_MANTISSA_LOOP; break;
		}
	}

	//=========================================================================
	// Overlong fractions end up here, so we need to scan until the end of the 
	// value
	//=========================================================================

	for (;;)
	{
		int chr = (int)(unsigned char) *p;

		switch (chr)
		{
		case '.': case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
			p ++;
			mantSize ++;
			break;

		default: goto END_MANTISSA_LOOP; break; 
		}
	}


END_MANTISSA_LOOP:

	if ((*p == 'E') || (*p == 'e'))
	{
		p ++;
		if (*p == '-') 
		{
			expSign = TRUE;
			expNeg = -1;
			p ++;
		} 
		else 
		{
			if (*p == '+') 
				p ++;
		}

		for (;;)
		{
			int chr = (int)(unsigned char) *p;

			switch (chr)
			{
			case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
				exp = 10 * exp + (chr - '0');
				p ++;
				break;

			default:
				goto END_EXPONENT_LOOP;
				break;
			}
		}
	}
	else
	{
		if (decPt == -1) 
		{
			ds->start = (char *) p;

			if (mantSize < 10)
			{
				// This number is 9 digits and will definitly fit within a 32 bit value
				//"685477580"
				return ds->dec->newInt( (JSINT32) frac1 * (JSINT32) fracNeg);
			}
			else
			if (mantSize < 19)
			{
				// This number is 18 digits and will definitly fit within a 64 bit value
				//"922337203685477580"
				return ds->dec->newLong( ( ( (JSINT64) frac1 * decPowerOf10[mantSize - 9]) + (JSINT64) frac2) * (JSINT64) fracNeg);
			}
			else
			if (mantSize > 19)
			{
				//This value is 20 digits and will not fit within a 64 bit value
				//"92233720368547758089"
				return SetError(ds, -1, fracNeg == -1 ? "Value is too small" : "Value is too big");
			}
			
			// 19 is the worst kind, try to figure out if this value will fit within a signed 64-bit value

			if (frac1 > 922337203)
			{
				return SetError(ds, -1, fracNeg == -1 ? "Value is too small" : "Value is too big");
			}
			else
			if (frac1 == 922337203)
			{
				if (frac2 > overflowLimit)
				{
					return SetError(ds, -1, fracNeg == -1 ? "Value is too small" : "Value is too big");
				}
			}
			
			return ds->dec->newLong( ( ( (JSINT64) frac1 * decPowerOf10[mantSize - 9]) + (JSINT64) frac2) * (JSINT64) fracNeg);
		}
	}

END_EXPONENT_LOOP:

	if (mantSize > 9)
		fraction = (frac1 * (double) decPowerOf10[mantSize - 9]) + frac2;
	else
		fraction = frac1;

	if (decPt == -1)
	{
		decPt = mantSize;
	}

	fracExp = decPt - mantSize;

	exp = fracExp + (exp * expNeg);


	/*
	* Generate a floating-point number that represents the exponent.
	* Do this by processing the exponent one bit at a time to combine
	* many powers of 2 of 10. Then combine the exponent with the
	* fraction.
	*/

	if (exp < 0) 
	{
		expSign = TRUE;
		exp = -exp;
	} 
	else 
	{
		expSign = FALSE;
	}
	if (exp > maxExponent) 
	{
		exp = maxExponent;
		//FIXME: errno = ERANGE;
	}

	dblExp = 1.0;

	for (d = powersOf10; exp != 0; exp >>= 1, d += 1) 
	{
		if (exp & 01) 
		{
			dblExp *= *d;
		}
	}

	if (expSign) 
	{
		fraction /= dblExp;
	} 
	else 
	{
		fraction *= dblExp;
	}

	ds->start = (char *) p;
	return ds->dec->newDouble(fraction * (double) fracNeg);
}

FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_true ( struct DecoderState *ds) 
{
	char *offset = ds->start;
	offset ++;

	if (*(offset++) != 'r')
		goto SETERROR;
	if (*(offset++) != 'u')
		goto SETERROR;
	if (*(offset++) != 'e')
		goto SETERROR;

	ds->lastType = JT_TRUE;
	ds->start = offset;
	RETURN_JSOBJ_NULLCHECK(ds->dec->newTrue());

SETERROR:
	return SetError(ds, -1, "Unexpected character found when decoding 'true'");
}

FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_false ( struct DecoderState *ds) 
{
	char *offset = ds->start;
	offset ++;

	if (*(offset++) != 'a')
		goto SETERROR;
	if (*(offset++) != 'l')
		goto SETERROR;
	if (*(offset++) != 's')
		goto SETERROR;
	if (*(offset++) != 'e')
		goto SETERROR;

	ds->lastType = JT_FALSE;
	ds->start = offset;
	RETURN_JSOBJ_NULLCHECK(ds->dec->newFalse());

SETERROR:
	return SetError(ds, -1, "Unexpected character found when decoding 'false'");

}


FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_null ( struct DecoderState *ds) 
{
	char *offset = ds->start;
	offset ++;

	if (*(offset++) != 'u')
		goto SETERROR;
	if (*(offset++) != 'l')
		goto SETERROR;
	if (*(offset++) != 'l')
		goto SETERROR;

	ds->lastType = JT_NULL;
	ds->start = offset;
	RETURN_JSOBJ_NULLCHECK(ds->dec->newNull());

SETERROR:
	return SetError(ds, -1, "Unexpected character found when decoding 'null'");
}

FASTCALL_ATTR void FASTCALL_MSVC SkipWhitespace(struct DecoderState *ds) 
{
	char *offset = ds->start;

	for (;;)
	{
		switch (*offset)
		{
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			offset ++;
			break;

		default:
			ds->start = offset;
			return;
		}
	}
}


enum DECODESTRINGSTATE
{
	DS_ISNULL = 0x32,
	DS_ISQUOTE,
	DS_ISESCAPE,
	DS_UTFLENERROR,

};

static const JSUINT8 g_decoderLookup[256] = 
{
	/* 0x00 */ DS_ISNULL, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	/* 0x10 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	/* 0x20 */ 1, 1, DS_ISQUOTE, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	/* 0x30 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	/* 0x40 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	/* 0x50 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, DS_ISESCAPE, 1, 1, 1,
	/* 0x60 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	/* 0x70 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	/* 0x80 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	/* 0x90 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	/* 0xa0 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	/* 0xb0 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	/* 0xc0 */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 
	/* 0xd0 */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	/* 0xe0 */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 
	/* 0xf0 */ 4, 4, 4, 4, 4, 4, 4, 4, DS_UTFLENERROR, DS_UTFLENERROR, DS_UTFLENERROR, DS_UTFLENERROR, DS_UTFLENERROR, DS_UTFLENERROR, DS_UTFLENERROR, DS_UTFLENERROR, 
};


FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_string ( struct DecoderState *ds)
{
	JSUTF16 sur[2] = { 0 };
	int iSur = 0;
	int index;
	wchar_t *escOffset;
	size_t escLen = (ds->escEnd - ds->escStart);
	JSUINT8 *inputOffset;
	JSUINT8 oct;
	JSUTF32 ucs;
	ds->lastType = JT_INVALID;
	ds->start ++;

	if ( (size_t) (ds->end - ds->start) > escLen)
	{
		size_t newSize = (ds->end - ds->start);

		if (ds->escHeap)
		{
			ds->escStart = (wchar_t *) ds->dec->realloc (ds->escStart, newSize * sizeof(wchar_t));
			if (!ds->escStart)
			{
				return SetError(ds, -1, "Could not reserve memory block");
			}
		}
		else
		{
			wchar_t *oldStart = ds->escStart;
			ds->escHeap = 1;
			ds->escStart = (wchar_t *) ds->dec->malloc (newSize * sizeof(wchar_t));
			if (!ds->escStart)
			{
				return SetError(ds, -1, "Could not reserve memory block");
			}
			memcpy (ds->escStart, oldStart, escLen * sizeof(wchar_t));
		}

		ds->escEnd = ds->escStart + newSize;
	}

	escOffset = ds->escStart;
	inputOffset = (JSUINT8 *) ds->start;

	for (;;)
	{
		switch (g_decoderLookup[(JSUINT8)(*inputOffset)])
		{
		case DS_ISNULL:
			return SetError(ds, -1, "Unmatched ''\"' when when decoding 'string'");

		case DS_ISQUOTE:
			ds->lastType = JT_UTF8;
			inputOffset ++;
			ds->start += ( (char *) inputOffset - (ds->start));
			RETURN_JSOBJ_NULLCHECK(ds->dec->newString(ds->escStart, escOffset));

		case DS_UTFLENERROR:
			return SetError (ds, -1, "Invalid UTF-8 sequence length when decoding 'string'");

		case DS_ISESCAPE:
			inputOffset ++;
			switch (*inputOffset)
			{
			case '\\': *(escOffset++) = L'\\'; inputOffset++; continue;
			case '\"': *(escOffset++) = L'\"'; inputOffset++; continue;
			case '/':  *(escOffset++) = L'/';  inputOffset++; continue;
			case 'b':  *(escOffset++) = L'\b'; inputOffset++; continue;
			case 'f':  *(escOffset++) = L'\f'; inputOffset++; continue;
			case 'n':  *(escOffset++) = L'\n'; inputOffset++; continue;
			case 'r':  *(escOffset++) = L'\r'; inputOffset++; continue;
			case 't':  *(escOffset++) = L'\t'; inputOffset++; continue;

			case 'u':
				{
					int index;
					inputOffset ++;

					for (index = 0; index < 4; index ++)
					{
						switch (*inputOffset)
						{
						case '\0':  return SetError (ds, -1, "Unterminated unicode escape sequence when decoding 'string'");
						default:        return SetError (ds, -1, "Unexpected character in unicode escape sequence when decoding 'string'");

						case '0':
						case '1':
						case '2':
						case '3':
						case '4':
						case '5':
						case '6':
						case '7':
						case '8':
						case '9':
							sur[iSur] = (sur[iSur] << 4) + (JSUTF16) (*inputOffset - '0');
							break;

						case 'a':
						case 'b':
						case 'c':
						case 'd':
						case 'e':
						case 'f':
							sur[iSur] = (sur[iSur] << 4) + 10 + (JSUTF16) (*inputOffset - 'a');
							break;

						case 'A':
						case 'B':
						case 'C':
						case 'D':
						case 'E':
						case 'F':
							sur[iSur] = (sur[iSur] << 4) + 10 + (JSUTF16) (*inputOffset - 'A');
							break;
						}

						inputOffset ++;
					}


					if (iSur == 0)
					{
						if((sur[iSur] & 0xfc00) == 0xd800)
						{
							// First of a surrogate pair, continue parsing
							iSur ++;
							break;
						} 
						(*escOffset++) = (wchar_t) sur[iSur];
						iSur = 0;
					}
					else
					{
						// Decode pair
						if ((sur[1] & 0xfc00) != 0xdc00)
						{
							return SetError (ds, -1, "Unpaired high surrogate when decoding 'string'");
						}

#if WCHAR_MAX == 0xffff
						(*escOffset++) = (wchar_t) sur[0];
						(*escOffset++) = (wchar_t) sur[1];
#else
						(*escOffset++) = (wchar_t) 0x10000 + (((sur[0] - 0xd800) << 10) | (sur[1] - 0xdc00));
#endif
						iSur = 0;
					}
					break;
				}

			case '\0': return SetError(ds, -1, "Unterminated escape sequence when decoding 'string'");
			default: return SetError(ds, -1, "Unrecognized escape sequence when decoding 'string'");
			}
			break;

		case 1:
			*(escOffset++) = (wchar_t) (*inputOffset++); 
			break;

		case 2:
			{
				ucs = (*inputOffset++) & 0x1f;
				ucs <<= 6;
				if (((*inputOffset) & 0x80) != 0x80)
				{
					return SetError(ds, -1, "Invalid octet in UTF-8 sequence when decoding 'string'");
				}
				ucs |= (*inputOffset++) & 0x3f;
				if (ucs < 0x80) return SetError (ds, -1, "Overlong 2 byte UTF-8 sequence detected when decoding 'string'");
				*(escOffset++) = (wchar_t) ucs;
				break;
			}

		case 3:
			{
				JSUTF32 ucs = 0;
				ucs |= (*inputOffset++) & 0x0f;

				for (index = 0; index < 2; index ++)
				{
					ucs <<= 6;
					oct = (*inputOffset++);

					if ((oct & 0x80) != 0x80)
					{
						return SetError(ds, -1, "Invalid octet in UTF-8 sequence when decoding 'string'");
					}

					ucs |= oct & 0x3f;
				}

				if (ucs < 0x800) return SetError (ds, -1, "Overlong 3 byte UTF-8 sequence detected when encoding string");
				*(escOffset++) = (wchar_t) ucs;
				break;
			}

		case 4:
			{
				JSUTF32 ucs = 0;
				ucs |= (*inputOffset++) & 0x07;

				for (index = 0; index < 3; index ++)
				{
					ucs <<= 6;
					oct = (*inputOffset++);

					if ((oct & 0x80) != 0x80)
					{
						return SetError(ds, -1, "Invalid octet in UTF-8 sequence when decoding 'string'");
					}

					ucs |= oct & 0x3f;
				}

				if (ucs < 0x10000) return SetError (ds, -1, "Overlong 4 byte UTF-8 sequence detected when decoding 'string'");

#if WCHAR_MAX == 0xffff
				if (ucs >= 0x10000)
				{
					ucs -= 0x10000;
					*(escOffset++) = (wchar_t) (ucs >> 10) + 0xd800;
					*(escOffset++) = (wchar_t) (ucs & 0x3ff) + 0xdc00;
				}
				else
				{
					*(escOffset++) = (wchar_t) ucs;
				}
#else
				*(escOffset++) = (wchar_t) ucs;
#endif
				break;
			}
		}
	}
}

FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_array( struct DecoderState *ds)
{
	JSOBJ itemValue;
	JSOBJ newObj = ds->dec->newArray();
	int len = 0;

	ds->lastType = JT_INVALID;
	ds->start ++;


	for (;;)
	{
		SkipWhitespace(ds);

		if ((*ds->start) == ']')
		{
			if (len == 0)
			{
				ds->start ++;
				return newObj;
			}

			ds->dec->releaseObject(newObj);
			return SetError(ds, -1, "Unexpected character found when decoding array value (1)");
		}

		itemValue = decode_any(ds);

		if (itemValue == NULL)
		{
			ds->dec->releaseObject(newObj);
			return NULL;
		}

		ds->dec->arrayAddItem (newObj, itemValue);

		SkipWhitespace(ds);

		switch (*(ds->start++))
		{
		case ']':
			return newObj;

		case ',':
			break;

		default:
			ds->dec->releaseObject(newObj);
			return SetError(ds, -1, "Unexpected character found when decoding array value (2)");
		}

		len ++;
	}
}



FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_object( struct DecoderState *ds)
{
	JSOBJ itemName;
	JSOBJ itemValue;
	JSOBJ newObj = ds->dec->newObject();

	ds->start ++;

	for (;;)
	{
		SkipWhitespace(ds);

		if ((*ds->start) == '}')
		{
			ds->start ++;
			return newObj;
		}

		ds->lastType = JT_INVALID;
		itemName = decode_any(ds);

		if (itemName == NULL)
		{
			ds->dec->releaseObject(newObj);
			return NULL;
		}

		if (ds->lastType != JT_UTF8)
		{
			ds->dec->releaseObject(newObj);
			ds->dec->releaseObject(itemName);
			return SetError(ds, -1, "Key name of object must be 'string' when decoding 'object'");
		}

		SkipWhitespace(ds);

		if (*(ds->start++) != ':')
		{
			ds->dec->releaseObject(newObj);
			ds->dec->releaseObject(itemName);
			return SetError(ds, -1, "No ':' found when decoding object value");
		}

		SkipWhitespace(ds);

		itemValue = decode_any(ds);

		if (itemValue == NULL)
		{
			ds->dec->releaseObject(newObj);
			ds->dec->releaseObject(itemName);
			return NULL;
		}

		ds->dec->objectAddKey (newObj, itemName, itemValue);

		SkipWhitespace(ds);

		switch (*(ds->start++))
		{
		case '}':
			return newObj;

		case ',':
			break;

		default:
			ds->dec->releaseObject(newObj);
			return SetError(ds, -1, "Unexpected character in found when decoding object value");
		}
	}
}

FASTCALL_ATTR JSOBJ FASTCALL_MSVC decode_any(struct DecoderState *ds)
{
	for (;;)
	{
		switch (*ds->start)
		{
		case '\"': 
			return decode_string (ds);
		case '0': 
		case '1':
		case '2': 
		case '3': 
		case '4': 
		case '5':
		case '6': 
		case '7': 
		case '8': 
		case '9': 
		case '-': 
			return decode_numeric (ds);

		case '[': return decode_array (ds);
		case '{': return decode_object (ds);
		case 't': return decode_true (ds);
		case 'f': return decode_false (ds);
		case 'n': return decode_null (ds);

		case ' ':
		case '\t':
		case '\r':
		case '\n':
			// White space
			ds->start ++;
			break;

		default:
			return SetError(ds, -1, "Expected object or value");
		}
	}
}

JSOBJ JSON_DecodeObject(JSONObjectDecoder *dec, const char *buffer, size_t cbBuffer)
{

	/*
	FIXME: Base the size of escBuffer of that of cbBuffer so that the unicode escaping doesn't run into the wall each time */
	struct DecoderState ds;
	wchar_t escBuffer[(JSON_MAX_STACK_BUFFER_SIZE / sizeof(wchar_t))];
	JSOBJ ret;

	ds.start = (char *) buffer;
	ds.end = ds.start + cbBuffer;

	ds.escStart = escBuffer;
	ds.escEnd = ds.escStart + (JSON_MAX_STACK_BUFFER_SIZE / sizeof(wchar_t));
	ds.escHeap = 0;
	ds.dec = dec;
	ds.dec->errorStr = NULL;
	ds.dec->errorOffset = NULL;

	ds.dec = dec;

	ret = decode_any (&ds);

	if (ds.escHeap)
	{
		dec->free(ds.escStart);
	}

	SkipWhitespace(&ds);

	if (ds.start != ds.end && ret)
	{
		dec->releaseObject(ret);    
		return SetError(&ds, -1, "Trailing data");
	}

	return ret;
}
