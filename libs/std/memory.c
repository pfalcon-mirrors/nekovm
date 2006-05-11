/* ************************************************************************ */
/*																			*/
/*  Neko Standard Library													*/
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
#include <neko.h>
#include <neko_mod.h>
#include <objtable.h>

/**
	<doc>
	<h1>Memory</h1>
	<p>
	An API for memory manipulation and statistics.
	</p>
	</doc>
**/

typedef struct _vtree {
	int_val v;
	struct _vtree *left;
	struct _vtree *right;
} vtree;

typedef struct {
	vtree **t;
	int s;
} vparams;

static int mem_cache( void *v, vtree **t ) {
	vtree *p = *t;
	vtree *prev = NULL;
	while( p != NULL ) {
		if( p->v == (int_val)v )
			return 1;
		prev = p;
		if( p->v > (int_val)v )
			p = p->left;
		else
			p = p->right;
	}
	p = (vtree*)alloc(sizeof(vtree));
	p->v = (int_val)v;
	p->left = NULL;
	p->right = NULL;
	if( prev == NULL )
		*t = p;
	else {
		if( prev->v > p->v )
			prev->left = p;
		else
			prev->right = p;
	}
	return 0;
}

static void mem_obj_field( value v, field f, void *_p );
static int mem_module( neko_module *m, vtree **l );

static int mem_size_rec( value v,  vtree **l ) {
	switch( val_type(v) ) {
	case VAL_INT:
	case VAL_BOOL:
	case VAL_NULL:
		return 0;
	case VAL_FLOAT:
		if( mem_cache(v,l) )
			return 0;
		return sizeof(vfloat);
	case VAL_STRING:
		if( mem_cache(v,l) )
			return 0;
		return sizeof(value) + val_strlen(v);
	case VAL_OBJECT:
		if( mem_cache(v,l) )
			return 0;
		{
			vparams p;
			p.t = l;
			p.s = sizeof(vobject);
#ifdef COMPACT_TABLE
			p.s += sizeof(struct _objtable);
#endif
			val_iter_fields(v,mem_obj_field,&p);
			if( ((vobject*)v)->proto != NULL )
				p.s += mem_size_rec((value)((vobject*)v)->proto,l);
			return p.s;
		}
	case VAL_ARRAY:
		if( mem_cache(v,l) )
			return 0;
		{
			int t = sizeof(value);
			int size = val_array_size(v);
			int i;
			t += size * sizeof(value);
			for(i=0;i<size;i++)
				t += mem_size_rec(val_array_ptr(v)[i],l);
			return t;
		}
	case VAL_FUNCTION:
		if( mem_cache(v,l) )
			return 0;
		{
			int t = sizeof(vfunction);
			t += mem_size_rec(((vfunction*)v)->env,l);
			if( val_tag(v) == VAL_PRIMITIVE )
				t += mem_size_rec((value)((vfunction*)v)->module,l);
			else
				t += mem_module(((vfunction*)v)->module,l);
			return t;
		}
	case VAL_ABSTRACT:
		{
			int t = sizeof(vabstract);
			if( val_kind(v) == neko_kind_module )
				t += mem_module((neko_module*)val_data(v),l);
			else if( val_kind(v) == k_hash ) {
				vhash *h = (vhash*)val_data(v);				
				int i;
				t += sizeof(vhash);
				t += sizeof(hcell*) * h->ncells;
				for(i=0;i<h->ncells;i++) {
					hcell *c = h->cells[i];
					while( c != NULL ) {
						t += sizeof(hcell);
						t += mem_size_rec(c->key,l);
						t += mem_size_rec(c->val,l);
						c = c->next;
					}
				}
			}	
			return t;
		}
	default:
		val_throw(alloc_string("mem_size : Unexpected value"));
		break;
	}
	return 0;
}

static void mem_obj_field( value v, field f, void *_p ) {
	vparams *p = (vparams*)_p;
#ifndef COMPACT_TABLE
	p->s += sizeof(struct _objtable);
#else
	p->s += sizeof(cell);
#endif
	p->s += mem_size_rec(v,p->t);
}

static int mem_module( neko_module *m, vtree **l ) {
	int t = 0;
	unsigned int i;
	if( mem_cache(m,l) )
		return 0;
	t += sizeof(neko_module);
	t += m->codesize * sizeof(int_val);
	t += m->nglobals * sizeof(int_val);
	for(i=0;i<m->nglobals;i++)
		t += mem_size_rec(m->globals[i],l);
	t += m->nfields * sizeof(value*);
	for(i=0;i<m->nfields;i++)
		t += mem_size_rec(m->fields[i],l);
	t += mem_size_rec(m->loader,l);
	t += mem_size_rec(m->exports,l);
	t += mem_size_rec(m->debuginf,l);
	t += mem_size_rec(m->name,l);
	return t;
}

/**
	mem_size : any -> int
	<doc>Calculate the quite precise amount of VM memory reachable from this value</doc>
**/
static value mem_size( value v ) {
	vtree *t = NULL;
	return alloc_int(mem_size_rec(v,&t));
}

DEFINE_PRIM(mem_size,1);

/* ************************************************************************ */
