/* ************************************************************************ */
/*																			*/
/*  Neko Virtual Machine													*/
/*  Copyright (c)2005 Motion-Twin											*/
/*																			*/
/* This library is free software; you can redistribute it and/or			*/
/* modify it under the terms of the GNU Lesser General Public				*/
/* License as published by the Free Software Foundation; either				*/
/* version 2.1 of the License, or (at your option) any later version.		*/
/*																			*/
/* This library is distributed in the hope that it will be useful,			*/
/* but WITHOUT ANY WARRANTY; without even the implied warranty of			*/
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU		*/
/* Lesser General Public License or the LICENSE file for more details.		*/
/*																			*/
/* ************************************************************************ */
#include <string.h>
#include "objtable.h"

int otable_remove( objtable *t, field id ) {
	int min = 0;
	int max = t->count;
	int mid;
	field cid;
	objcell *c = t->cells;
	if( !max )
		return 0;
	while( min < max ) {
		mid = (min + max) >> 1;
		cid = c[mid].id;
		if( cid < id )
			min = mid + 1;
		else if( cid > id )
			max = mid;
		else {
			t->count--;
			while( mid < t->count ) {
				c[mid] = c[mid+1];
				mid++;
			}
			c[mid].v = val_null;
			return 1;
		}
	}
	return 0;
}

void otable_optimize( objtable *t ) {
	int max = t->count;
	int i;
	int cur = 0;
	objcell *c = t->cells;
	for(i=0;i<max;i++) {
		value v = c[i].v;
		if( v != val_null )
			c[cur++] = c[i];
	}
	for(i=cur;i<max;i++)
		c[i].v = NULL;
	t->count = cur;
}

void otable_replace( objtable *t, field id, value data ) {
	int min = 0;
	int max = t->count;
	int mid;
	field cid;
	objcell *c = t->cells;
	while( min < max ) {
		mid = (min + max) >> 1;
		cid = c[mid].id;
		if( cid < id )
			min = mid + 1;
		else if( cid > id )
			max = mid;
		else {
			c[mid].v = data;
			return;
		}
	}
	mid = (min + max) >> 1;
	c = (objcell*)alloc(sizeof(objcell)*(t->count + 1));
	min = 0;
	while( min < mid ) {
		c[min] = t->cells[min];
		min++;
	}
	c[mid].id = id;
	c[mid].v = data;
	while( min < t->count ) {
		c[min+1] = t->cells[min];
		min++;
	}
	t->cells = c;
	t->count++;
}

void otable_copy( objtable *t, objtable *target ) {
	target->count = t->count;
	target->cells = (objcell*)alloc(sizeof(objcell)*t->count);
	memcpy(target->cells,t->cells,sizeof(objcell)*t->count);
}

void otable_iter(objtable *t, void f( value data, field id, void *), void *p ) {
	int i;
	int n = t->count;
	objcell *c = t->cells;
	for(i=0;i<n;i++)
		f(c[i].v,c[i].id,p);
}

/* ************************************************************************ */
