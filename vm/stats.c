/* ************************************************************************ */
/*																			*/
/*  Neko Virtual Machine													*/
/*  Copyright (c)2005-2008 Motion-Twin										*/
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
#include <malloc.h>
#include <stdio.h>
#include <time.h>
#include "neko_vm.h"
#ifdef NEKO_WINDOWS
#	include <windows.h>
#else
#	include <sys/time.h>
#endif

typedef struct _statinfos {
	const char *kind;
	int ksize;
	int ncalls;
	int nerrors;
	int subtime;
	int totaltime;
	int starttime;
	struct _statinfos *stack;
	struct _statinfos *next;
} statinfos;

static statinfos *list = NULL;
static statinfos *stack = NULL;
static int init_done = 0;

static int precise_timer() {
#	ifdef NEKO_WINDOWS
	LARGE_INTEGER t;
	static LARGE_INTEGER freq;
	if( !init_done ) {
		DWORD procs, sm, procid = 1;
		// ensure that we always use the same processor
		// or else, performance counter might vary depending
		// on the current CPU
		GetProcessAffinityMask(GetCurrentProcess(),&procs,&sm);
		while( !(procs & procid) )
			procid <<= 1;
		SetProcessAffinityMask(GetCurrentProcess(),procid);
		init_done = 1;
		QueryPerformanceFrequency(&freq);
	}
	QueryPerformanceCounter(&t);
	return (int)( t.QuadPart * 1000000 / freq.QuadPart );
#	else
	static int base_sec;
	struct timeval tv;
	gettimeofday(&tv,NULL);
	if( !init_done ) {
		init_done = 1;
		base_sec = tv.tv_sec;
	}
	return (tv.tv_sec - base_sec) * 1000000 + tv.tv_usec;
#	endif
}

void neko_stats_measure( neko_vm *vm, const char *kind, int start ) {
	int ksize = strlen(kind);
	statinfos *s;
	if( start ) {
		int time = precise_timer();
		// lookup in list
		s = list;
		while( s ) {
			if( ksize == s->ksize && s->starttime == 0 && memcmp(kind,s->kind,ksize) == 0 )
				break;
			s = s->next;
		}
		// init
		if( s == NULL ) {
			s = (statinfos*)malloc(sizeof(statinfos));
			s->kind = strdup(kind);
			s->ksize = ksize;
			s->ncalls = 0;
			s->nerrors = 0;
			s->totaltime = 0;
			s->subtime = 0;
			s->next = list;
			list = s;
		}
		// add to stack
		s->ncalls++;
		s->stack = stack;
		stack = s;		
		s->starttime = time;
	} else {
		// lookup on stack
		s = stack;
		while( s ) {
			statinfos *next;
			if( ksize == s->ksize && memcmp(kind,s->kind,ksize) == 0 )
				break;
			next = s->stack;
			s->nerrors++; // stop was not done (exception)
			s->starttime = 0;
			s = next;
		}
		// pop from stack
		if( s ) {
			int delta = precise_timer() - s->starttime;
			s->totaltime += delta;
			stack = s->stack;
			if( stack ) stack->subtime += delta;
			s->starttime = 0;
		} else
			stack = NULL;
	}
}

// merged-sort for linked list
static int cmp( statinfos *a, statinfos *b ) {
	int delta = a->totaltime - b->totaltime;
	if( delta == 0 ) return b->ncalls - a->ncalls;
	return delta;
}
static statinfos *sort( statinfos *list ) {
    statinfos *p, *q, *e, *tail, *oldhead;
    int insize, nmerges, psize, qsize, i;
    insize = 1;
    while( list ) {
		p = list;
		oldhead = list;
		list = NULL;
		tail = NULL;
		nmerges = 0;
		while( p ) {
			nmerges++;
			q = p;
			psize = 0;
			for(i=0;i<insize;i++) {
				psize++;
				q = q->next;
				if (!q) break;
			}
			qsize = insize;
			while (psize > 0 || (qsize > 0 && q)) {
				if( psize == 0 ) {
					e = q; q = q->next; qsize--;					
				} else if( qsize == 0 || !q ) {
					e = p; p = p->next; psize--;					
				} else if( cmp(p,q) <= 0 ) {
					e = p; p = p->next; psize--;					
				} else {
					e = q; q = q->next; qsize--;
				}
				if( tail )
					tail->next = e;
				else
					list = e;
				tail = e;
			}
			p = q;
		}
		tail->next = NULL;
		if( nmerges <= 1 )
            return list;        
        insize *= 2;
    }
	return NULL;
}

value neko_stats_build( neko_vm *vm ) {
	value v = val_null;
	statinfos *s = list;
	// merge duplicates
	while( s ) {
		statinfos *s2 = s->next, *prev = s;
		while( s2 ) {
			if( s2->ksize == s->ksize && memcmp(s->kind,s2->kind,s->ksize) == 0 ) {
				s->nerrors += s2->nerrors;
				s->ncalls += s2->ncalls;
				s->totaltime += s2->totaltime;
				s->subtime += s2->subtime;
				prev->next = s2->next;
				free(s2);
				s2 = prev->next;
			} else {
				prev = s2;
				s2 = s2->next;
			}
		}
		s = s->next;
	}
	list = sort(list);
	s = list;
	while( s ) {
		value tmp = alloc_array(6);
		val_array_ptr(tmp)[0] = alloc_string(s->kind);
		val_array_ptr(tmp)[1] = alloc_int(s->totaltime);
		val_array_ptr(tmp)[2] = alloc_int(s->totaltime - s->subtime);
		val_array_ptr(tmp)[3] = alloc_int(s->ncalls);
		val_array_ptr(tmp)[4] = alloc_int(s->nerrors);
		val_array_ptr(tmp)[5] = v;
		v = tmp;
		s = s->next;
	}
	return v;
}
