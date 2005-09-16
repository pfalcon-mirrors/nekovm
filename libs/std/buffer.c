/* ************************************************************************ */
/*																			*/
/*  Neko Standard Library													*/
/*  Copyright (c)2005 Nicolas Cannasse										*/
/*																			*/
/*  This program is free software; you can redistribute it and/or modify	*/
/*  it under the terms of the GNU General Public License as published by	*/
/*  the Free Software Foundation; either version 2 of the License, or		*/
/*  (at your option) any later version.										*/
/*																			*/
/*  This program is distributed in the hope that it will be useful,			*/
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of			*/
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the			*/
/*  GNU General Public License for more details.							*/
/*																			*/
/*  You should have received a copy of the GNU General Public License		*/
/*  along with this program; if not, write to the Free Software				*/
/*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA */
/*																			*/
/* ************************************************************************ */
#include <neko.h>

DEFINE_KIND(k_buffer);

static value buffer_alloc() {
	buffer b = alloc_buffer(NULL);
	return alloc_abstract(k_buffer,b);
}

static value buffer_add( value b, value v ) {
	val_check_kind(b,k_buffer);
	val_buffer( (buffer)val_data(b), v );
	return val_true;
}

static value buffer_add_sub( value b, value v, value p, value l ) {
	val_check_kind(b,k_buffer);
	val_check(v,string);
	val_check(p,int);
	val_check(l,int);	
	if( val_int(p) < 0 || val_int(l) < 0 )
		return val_null;
	if( val_strlen(v) < val_int(p) || val_strlen(v) < val_int(p) + val_int(l) )
		return val_null;
	buffer_append_sub( (buffer)val_data(b), val_string(v) + val_int(p) , val_int(l) );
	return val_true;
}

static value buffer_string( value b ) {
	val_check_kind(b,k_buffer);
	return buffer_to_string( (buffer)val_data(b) );
}

DEFINE_PRIM(buffer_alloc,0);
DEFINE_PRIM(buffer_add,2);
DEFINE_PRIM(buffer_add_sub,4);
DEFINE_PRIM(buffer_string,1);

/* ************************************************************************ */