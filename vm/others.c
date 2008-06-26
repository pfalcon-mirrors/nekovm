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
#include <stdio.h>
#include "neko.h"
#include "objtable.h"
#include "vm.h"

#if !defined(NEKO_THREADS)

#include <stdlib.h>

struct _mt_local {
	void *value;
};

#elif defined(NEKO_WINDOWS)
#	include <windows.h>
	// disable warnings for type conversions
#	pragma warning(disable : 4311)
#	pragma warning(disable : 4312)
#else
#	include <stdlib.h>
#	include <pthread.h>

struct _mt_local {
	pthread_key_t key;
};

struct _mt_lock {
	pthread_mutex_t lock;
};

#endif

#define C(x,y)	((x << 8) | y)

DEFINE_KIND(k_int32);
DEFINE_KIND(k_hash);

extern mt_lock *neko_fields_lock;
extern objtable *neko_fields;
extern field id_compare;
extern field id_string;
extern char *jit_handle_trap;
typedef void (*jit_handle)( neko_vm * );

static INLINE int icmp( int a, int b ) {
	return (a == b)?0:((a < b)?-1:1);
}

static INLINE int fcmp( tfloat a, tfloat b ) {
	return (a == b)?0:((a < b)?-1:1);
}

static INLINE int scmp( const char *s1, int l1, const char *s2, int l2 ) {
	int r = memcmp(s1,s2,(l1 < l2)?l1:l2);
	return r?r:icmp(l1,l2);
}

EXTERN int val_compare( value a, value b ) {
	char tmp_buf[32];
	switch( C(val_type(a),val_type(b)) ) {
	case C(VAL_INT,VAL_INT):
		return icmp(val_int(a),val_int(b));
	case C(VAL_INT,VAL_FLOAT):
		return fcmp(val_int(a),val_float(b));
	case C(VAL_INT,VAL_STRING):
		return scmp(tmp_buf,sprintf(tmp_buf,"%d",val_int(a)),val_string(b),val_strlen(b));
	case C(VAL_FLOAT,VAL_INT):
		return fcmp(val_float(a),val_int(b));
	case C(VAL_FLOAT,VAL_FLOAT):
		return fcmp(val_float(a),val_float(b));
	case C(VAL_FLOAT,VAL_STRING):
		return scmp(tmp_buf,sprintf(tmp_buf,"%.10g",val_float(a)),val_string(b),val_strlen(b));
	case C(VAL_STRING,VAL_INT):
		return scmp(val_string(a),val_strlen(a),tmp_buf,sprintf(tmp_buf,"%d",val_int(b)));
	case C(VAL_STRING,VAL_FLOAT):
		return scmp(val_string(a),val_strlen(a),tmp_buf,sprintf(tmp_buf,"%.10g",val_float(b)));
	case C(VAL_STRING,VAL_BOOL):
		return scmp(val_string(a),val_strlen(a),val_bool(b)?"true":"false",val_bool(b)?4:5);
	case C(VAL_BOOL,VAL_STRING):
		return scmp(val_bool(a)?"true":"false",val_bool(a)?4:5,val_string(b),val_strlen(b));
	case C(VAL_STRING,VAL_STRING):
		return scmp(val_string(a),val_strlen(a),val_string(b),val_strlen(b));
	case C(VAL_OBJECT,VAL_OBJECT):
		if( a == b )
			return 0;
		{
			value tmp = val_field(a,id_compare);
			if( tmp == val_null )
				return invalid_comparison;
			a = val_callEx(a,tmp,&b,1,NULL);
		}
		if( val_is_int(a) )
			return val_int(a);
		return invalid_comparison;
	default:
		if( a == b )
			return 0;
		return invalid_comparison;
	}
}

typedef struct _stringitem {
	char *str;
	int size;
	int len;
	struct _stringitem *next;
} * stringitem;

struct _buffer {
	int totlen;
	int blen;
	stringitem data;
};

EXTERN buffer alloc_buffer( const char *init ) {
	buffer b = (buffer)alloc(sizeof(struct _buffer));
	b->totlen = 0;
	b->blen = 16;
	b->data = NULL;
	if( init )
		buffer_append(b,init);
	return b;
}

static void buffer_append_new( buffer b, const char *s, int len ) {
	int size;
	stringitem it;
	while( b->totlen >= (b->blen << 2) )
		b->blen <<= 1;
	size = (len < b->blen)?b->blen:len;
	it = (stringitem)alloc(sizeof(struct _stringitem));
	it->str = alloc_private(size);
	memcpy(it->str,s,len);
	it->size = size;
	it->len = len;
	it->next = b->data;
	b->data = it;	
}

EXTERN void buffer_append_sub( buffer b, const char *s, int_val _len ) {
	stringitem it;
	int len = (int)_len;
	if( s == NULL || len <= 0 )
		return;
	b->totlen += len;
	it = b->data;
	if( it ) {
		int free = it->size - it->len;
		if( free >= len ) {
			memcpy(it->str + it->len,s,len);
			it->len += len;
			return;
		} else {
			memcpy(it->str + it->len,s,free);
			it->len += free;
			s += free;
			len -= free;
		}
	}
	buffer_append_new(b,s,len);
}

EXTERN void buffer_append( buffer b, const char *s ) {
	if( s == NULL )
		return;
	buffer_append_sub(b,s,strlen(s));
}

EXTERN void buffer_append_char( buffer b, char c ) {
	stringitem it;
	b->totlen++;
	it = b->data;
	if( it && it->len != it->size ) {
		it->str[it->len++] = c;		
		return;
	}
	buffer_append_new(b,&c,1);
}

EXTERN value buffer_to_string( buffer b ) {
	value v = alloc_empty_string(b->totlen);
	stringitem it = b->data;
	char *s = (char*)val_string(v) + b->totlen;
	while( it != NULL ) {
		stringitem tmp;
		s -= it->len;
		memcpy(s,it->str,it->len);
		tmp = it->next;
		it = tmp;
	}
	return v;
}

typedef struct vlist {
	value v;
	struct vlist *next;
} vlist;

typedef struct vlist2 {
	value v;
	struct vlist *next;
	buffer b;
	int prev;
} vlist2;

static void val_buffer_rec( buffer b, value v, vlist *stack );

static void val_buffer_fields( value v, field f, void *_l ) {
	vlist2 *l = (vlist2*)_l;
	if( l->prev )
		buffer_append_sub(l->b,", ",2);
	else {
		buffer_append_sub(l->b," ",1);
		l->prev = 1;
	}
	val_buffer(l->b,val_field_name(f));
	buffer_append_sub(l->b," => ",4);
	val_buffer_rec(l->b,v,(vlist*)l);
}

static void val_buffer_rec( buffer b, value v, vlist *stack ) {
	char buf[32];
	int i, l;
	vlist *vtmp = stack;
	while( vtmp != NULL ) {
		if( vtmp->v == v ) {
			buffer_append_sub(b,"...",3);
			return;
		}
		vtmp = vtmp->next;
	}
	switch( val_type(v) ) {
	case VAL_INT:
		buffer_append_sub(b,buf,sprintf(buf,"%d",val_int(v)));
		break;
	case VAL_STRING:
		buffer_append_sub(b,val_string(v),val_strlen(v));
		break;
	case VAL_FLOAT:
		buffer_append_sub(b,buf,sprintf(buf,"%.10g",val_float(v)));
		break;
	case VAL_NULL:
		buffer_append_sub(b,"null",4);
		break;
	case VAL_BOOL:
		if( val_bool(v) )
			buffer_append_sub(b,"true",4);
		else
			buffer_append_sub(b,"false",5);
		break;
	case VAL_FUNCTION:
		buffer_append_sub(b,buf,sprintf(buf,"#function:%d",val_fun_nargs(v)));
		break;
	case VAL_OBJECT:
		{
			value s = val_field(v,id_string);
			if( s != val_null )
				s = val_callEx(v,s,NULL,0,NULL);
			if( val_is_string(s) )
				buffer_append_sub(b,val_string(s),val_strlen(s));
			else {
				vlist2 vtmp;
				vtmp.v = v;
				vtmp.next = stack;
				vtmp.b = b;
				vtmp.prev = 0;
				buffer_append_sub(b,"{",1);
				val_iter_fields(v,val_buffer_fields,&vtmp);
				if( vtmp.prev )
					buffer_append_sub(b," }",2);
				else
					buffer_append_sub(b,"}",1);
			}
			break;
		}
	case VAL_ARRAY:
		buffer_append_sub(b,"[",1);
		l = val_array_size(v);
		{
			vlist vtmp;
			vtmp.v = v;
			vtmp.next = stack;
			for(i=0;i<l;i++) {
				value vi = val_array_ptr(v)[i];
				val_buffer_rec(b,vi,&vtmp);
				if( i != l - 1 )
					buffer_append_sub(b,",",1);
			}
		}
		buffer_append_sub(b,"]",1);
		break;
	case VAL_ABSTRACT:
		if( val_is_kind(v,k_int32) )
			buffer_append_sub(b,buf,sprintf(buf,"%d",val_int32(v)));
		else
			buffer_append_sub(b,"#abstract",9);
		break;
	default:
		buffer_append_sub(b,"#unknown",8);
		break;
	}
}

EXTERN void val_buffer( buffer b, value v ) {
	val_buffer_rec(b,v,NULL);
}

// theses two 'append' function belongs here because we don't want
// them to be inlined in interp.c by GCC since this will break
// register allocation

value neko_append_int( neko_vm *vm, value str, int x, bool way ) {
	int len, len2;
	value v;
	len = val_strlen(str);
	len2 = sprintf(vm->tmp,"%d",x);
	v = alloc_empty_string(len+len2);
	if( way ) {
		memcpy((char*)val_string(v),val_string(str),len);
		memcpy((char*)val_string(v)+len,vm->tmp,len2+1);
	} else {
		memcpy((char*)val_string(v),vm->tmp,len2);
		memcpy((char*)val_string(v)+len2,val_string(str),len+1);
	}
	return v;
}

value neko_append_strings( value s1, value s2 ) {
	int len1 = val_strlen(s1);
	int len2 = val_strlen(s2);
	value v = alloc_empty_string(len1+len2);
	memcpy((char*)val_string(v),val_string(s1),len1);
	memcpy((char*)val_string(v)+len1,val_string(s2),len2+1);
	return v;
}

int neko_stack_expand( int_val *sp, int_val *csp, neko_vm *vm ) {
	int i;
	int size = (int)((((int_val)vm->spmax - (int_val)vm->spmin) / sizeof(int_val)) << 1);
	int_val *nsp;

	if( size > MAX_STACK_SIZE ) {
		vm->sp = sp;
		vm->csp = csp;
		return 0;
	}

	nsp = (int_val*)alloc(size * sizeof(int_val));

	// csp size
	i = (int)(((int_val)(csp + 1) - (int_val)vm->spmin) / sizeof(int_val));
	memcpy(nsp,vm->spmin,sizeof(int_val) * i);
	vm->csp = nsp + i - 1;

	// sp size
	i = (int)(((int_val)vm->spmax - (int_val)sp) / sizeof(int_val));
	memcpy(nsp+size-i,sp,sizeof(int_val) * i);
	vm->sp = nsp + size - i;
	vm->spmin = nsp;
	vm->spmax = nsp + size;
	return 1;
}

EXTERN field val_id( const char *name ) {
	value *fdata;
	field f;
	value acc = alloc_int(0);
	const char *oname = name;
	while( *name ) {
		acc = alloc_int(223 * val_int(acc) + *((unsigned char*)name));
		name++;
	}
	f = val_int(acc);
	lock_acquire(neko_fields_lock);
	fdata = otable_find(*neko_fields,f);
	if( fdata != NULL ) {
		if( scmp(val_string(*fdata),val_strlen(*fdata),oname,(int)(name - oname)) != 0 ) {
			buffer b = alloc_buffer("Field conflict between ");
			val_buffer(b,*fdata);
			buffer_append(b," and ");
			buffer_append(b,oname);
			lock_release(neko_fields_lock);
			bfailure(b);
		}
	} else
		otable_replace(*neko_fields,f,copy_string(oname,name - oname));
	lock_release(neko_fields_lock);
	return f;
}

EXTERN value val_field_name( field id ) {
	value *fdata;
	lock_acquire(neko_fields_lock);
	fdata = otable_find(*neko_fields,id);
	lock_release(neko_fields_lock);
	if( fdata == NULL )
		return val_null;
	return *fdata;
}

EXTERN value val_field( value _o, field id ) {
	value *f;
	vobject *o = (vobject*)_o;
	do {
		f = otable_find(o->table,id);
		if( f != NULL )
			return *f;
		o = o->proto;
	} while( o );
	return val_null;
}

EXTERN void val_iter_fields( value o, void f( value , field, void * ) , void *p ) {
	otable_iter( ((vobject*)o)->table, f, p );
}

EXTERN void val_print( value v ) {
	neko_vm *vm;
	if( !val_is_string(v) ) {
		buffer b = alloc_buffer(NULL);
		val_buffer(b,v);
		v = buffer_to_string(b);
	}
	vm = NEKO_VM();
	vm->print( val_string(v), val_strlen(v), vm->print_param );
}

EXTERN void val_throw( value v ) {
	neko_vm *vm = NEKO_VM();
	vm->exc_stack = alloc_array(0);
	vm->vthis = v;
	if( *(char**)vm->start == jit_handle_trap )
		((jit_handle)jit_handle_trap)(vm);
	else
		longjmp(vm->start,1);
}

EXTERN void val_rethrow( value v ) {
	neko_vm *vm = NEKO_VM();
	vm->vthis = v;
	if( *(char**)vm->start == jit_handle_trap )
		((jit_handle)jit_handle_trap)(vm);
	else
		longjmp(vm->start,1);
}

static value failure_to_string() {
	value o = val_this();
	buffer b = alloc_buffer(NULL);
	val_check(o,object);
	val_buffer(b,val_field(o,val_id("file")));
	buffer_append(b,"(");
	val_buffer(b,val_field(o,val_id("line")));
	buffer_append(b,") : ");
	val_buffer(b,val_field(o,val_id("msg")));
	return buffer_to_string(b);
}

EXTERN void _neko_failure( value msg, const char *file, int line ) {
	char *fname = strrchr(file,'/');
	char *fname2 = strrchr(file,'\\');
	value o = alloc_object(NULL);
	if( fname2 > fname )
		fname = fname2;
	alloc_field(o,val_id("msg"),msg);
	alloc_field(o,val_id("file"),alloc_string(fname?(fname+1):file));
	alloc_field(o,val_id("line"),alloc_int(line));
	alloc_field(o,id_string,alloc_function(failure_to_string,0,"failure_to_string"));
	val_throw(o);
}

/* ------------------------------------------------------------------------ */
// MULTITHREADING

EXTERN mt_local *alloc_local() {
#	if !defined(NEKO_THREADS)
	mt_local *l = malloc(sizeof(mt_local));
	l->value = NULL;
	return l;
#	elif defined(NEKO_WINDOWS)
	DWORD t = TlsAlloc();
	TlsSetValue(t,NULL);
	return (mt_local*)t;
#	else
	mt_local *l = malloc(sizeof(mt_local));
	pthread_key_create(&l->key,NULL);
	return l;
#	endif
}

EXTERN void free_local( mt_local *l ) {
#	if !defined(NEKO_THREADS)
	free(l);
#	elif defined(NEKO_WINDOWS)
	TlsFree((DWORD)l);
#	else
	pthread_key_delete(l->key);
	free(l);
#	endif
}

EXTERN void local_set( mt_local *l, void *v ) {
#	if !defined(NEKO_THREADS)
	l->value = v;
#	elif defined(NEKO_WINDOWS)
	TlsSetValue((DWORD)l,v);
#	else
	pthread_setspecific(l->key,v);
#	endif
}

EXTERN void *local_get( mt_local *l ) {
	if( l == NULL )
		return NULL;
#	if !defined(NEKO_THREADS)
	return l->value;
#	elif defined(NEKO_WINDOWS)
	return (void*)TlsGetValue((DWORD)l);
#	else
	return pthread_getspecific(l->key);
#	endif
}

EXTERN mt_lock *alloc_lock() {
#	if !defined(NEKO_THREADS)
	return (mt_lock*)1;
#	elif defined(NEKO_WINDOWS)
	return (mt_lock*)CreateMutex(NULL,FALSE,NULL);
#	else
	mt_lock *l = malloc(sizeof(mt_lock));
	pthread_mutex_init(&l->lock,NULL);
	pthread_mutex_unlock(&l->lock);
	return l;
#	endif
}

EXTERN void lock_acquire( mt_lock *l ) {
#	if !defined(NEKO_THREADS)
#	elif defined(NEKO_WINDOWS)
	WaitForSingleObject((HANDLE)l,INFINITE);
#	else
	pthread_mutex_lock(&l->lock);
#	endif
}

EXTERN void lock_release( mt_lock *l ) {
#	if !defined(NEKO_THREADS)
#	elif defined(NEKO_WINDOWS)
	ReleaseMutex((HANDLE)l);
#	else
	pthread_mutex_unlock(&l->lock);
#	endif
}

EXTERN void free_lock( mt_lock *l ) {
#	if !defined(NEKO_THREADS)
#	elif defined(NEKO_WINDOWS)
	CloseHandle((HANDLE)l);
#	else
	pthread_mutex_destroy(&l->lock);
#	endif
}

/* ************************************************************************ */
