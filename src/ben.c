/*
Copyright 2006 Aiko Barz

This file is part of masala.

masala is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

masala is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with masala.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <semaphore.h>

#include "ben.h"

BEN *ben_init( int type ) {
	BEN *node = (BEN *) myalloc( sizeof(BEN), "ben_init" );

	node->t = type;

	switch( type ) {
		case BEN_STR:
			node->v.s = NULL;
			break;
		case BEN_INT:
			node->v.i = 0;
			break;
		case BEN_DICT:
			node->v.d = list_init();
			break;
		case BEN_LIST:
			node->v.l = list_init();
			break;
	}

	return node;
}

void ben_free( BEN *node ) {
	if( node == NULL )
		return;

	/* Delete recursively */
	ben_free_r( node );

	/* Delete the last node */
	myfree( node, "ben_free" );
}

void ben_free_r( BEN *node ) {
	ITEM *item = NULL;

	if( node == NULL )
		return;

	switch( node->t ) {
		case BEN_DICT:
			if( node->v.d != NULL ) {
				item = list_start( node->v.d );
				while( item != NULL ) {
					item = ben_free_item( node, item );
				}

				list_free( node->v.d );
			}
			break;

		case BEN_LIST:
			if( node->v.l != NULL ) {
				item = list_start( node->v.l );
				while( item != NULL ) {
					item = ben_free_item( node, item );
				}

				list_free( node->v.l );
			}
			break;
		case BEN_STR:
			if( node->v.s != NULL )
				str_free( node->v.s );
			break;
	}
}

ITEM *ben_free_item( BEN *node, ITEM *item ) {
	if( node == NULL || item == NULL )
		return NULL;
	if( node->t != BEN_DICT && node->t != BEN_LIST )
		return NULL;
	if( node->t == BEN_DICT && node->v.d == NULL )
		return NULL;
	if( node->t == BEN_LIST && node->v.l == NULL )
		return NULL;
	
	/* Remove key in case of BEN_DICT */
	if( node->t == BEN_DICT ) {
		tuple_free( item->val );
		item->val = NULL;
	} else {
		/* Remove ben object */
		ben_free_r( item->val );
		myfree( item->val, "ben_free_item" );
		item->val = NULL;
	}
	
	return list_del( node->v.d, item );
}

struct obj_raw *raw_init( void ) {
	struct obj_raw *raw = (struct obj_raw *) myalloc( sizeof(struct obj_raw), "raw_init" );
	raw->code = NULL;
	raw->size = 0;
	raw->p = NULL;
	return raw;
}

void raw_free( struct obj_raw *raw ) {
	myfree( raw->code, "raw_free" );
	myfree( raw, "raw_free" );
}
 

struct obj_tuple *tuple_init( BEN *key, BEN *val ) {
	struct obj_tuple *tuple = (struct obj_tuple *) myalloc( sizeof(struct obj_tuple), "tuple_init" );
	tuple->key = key;
	tuple->val = val;
	return tuple;
}

void tuple_free( struct obj_tuple *tuple ) {
	ben_free( tuple->key );
   	ben_free( tuple->val );
	myfree( tuple, "tuple_item" );
}

void ben_dict( BEN *node, BEN *key, BEN *val ) {
	struct obj_tuple *tuple = NULL;

	if( node == NULL )
		fail( "ben_dict( 1 )" );
	if( node->t != BEN_DICT)
		fail( "ben_dict( 2 )" );
	if( node->v.d == NULL )
		fail( "ben_dict( 3 )" );
	if( key == NULL )
		fail( "ben_dict( 4 )" );
	if( key->t != BEN_STR)
		fail( "ben_dict( 5 )" );
	if( val == NULL )
		fail( "ben_dict( 6 )" );

	tuple = tuple_init( key, val );
	
	if( list_put( node->v.d, tuple) == NULL )
		fail( "ben_dict( 7 )" );
}

void ben_list( BEN *node, BEN *val ) {
	if( node == NULL )
		fail( "ben_list( 1 )" );
	if( node->t != BEN_LIST)
		fail( "ben_list( 2 )" );
	if( node->v.l == NULL )
		fail( "ben_list( 3 )" );
	if( val == NULL )
		fail( "ben_list( 4 )" );

	if( list_put( node->v.l, val) == NULL )
		fail( "ben_list( 5 )" );
}

void ben_str( BEN *node, UCHAR *str, long int len ) {
	if( node == NULL )
		fail( "ben_str( 1 )" );
	if( node->t != BEN_STR)
		fail( "ben_str( 2 )" );
	if( str == NULL )
		fail( "ben_str( 3 )" );
	if( len < 0 )
		fail( "ben_str( 4 )" );

	node->v.s = str_init( str, len );
}

void ben_int( BEN *node, long int i ) {
	if( node == NULL )
		fail( "ben_int( 1 )" );
	if( node->t != BEN_INT)
		fail( "ben_int( 2 )" );
	
	node->v.i = i;
}

long int ben_enc_size( BEN *node ) {
	ITEM *item = NULL;
	struct obj_tuple *tuple = NULL;
	char buf[MAIN_BUF+1];
	long int size = 0;

	if( node == NULL )
		return size;

	switch( node->t ) {
		case BEN_DICT:
			size += 2; /* de */
			
			if( node->v.d == NULL )
				return size;

			if( node->v.d->item == NULL )
				return size;

			item = list_start( node->v.d );
			do {
				tuple = list_value( item );

				if( tuple->key != NULL && tuple->val != NULL ) {
					size += ben_enc_size( tuple->key );
					size += ben_enc_size( tuple->val );
				}
				
				item = list_next( item );
				
			} while( item != NULL );

			break;

		case BEN_LIST:
			size += 2; /* le */

			if( node->v.l == NULL )
				return size;

			if( node->v.l->item == NULL )
				return size;

			item = list_start( node->v.l );
			do {
				if( item->val != NULL ) {
					size += ben_enc_size( item->val );
				}
				item = list_next( item );
				
			} while( item != NULL );

			break;

		case BEN_INT:
			snprintf( buf, MAIN_BUF+1, "i%lie", node->v.i );
			size += strlen( buf );
			break;

		case BEN_STR:
			snprintf( buf, MAIN_BUF+1, "%li:", node->v.s->i );
			size += strlen( buf ) + node->v.s->i;
			break;

	}

	return size;
}

struct obj_raw *ben_enc( BEN *node ) {
	struct obj_raw *raw = raw_init();

	/* Calculate size of ben data */
	raw->size = ben_enc_size( node );
	if( raw->size <= 0 ) {
		raw_free( raw );
		return NULL;
	}

	/* Encode ben object */
	raw->code = (UCHAR *) myalloc( (raw->size) * sizeof(UCHAR), "ben_enc" );
	raw->p = ben_enc_rec( node,raw->code );
	if( raw->p == NULL ||( long int)(raw->p-raw->code) != raw->size ) {
		raw_free( raw );
		return NULL;
	}

	return raw;
}

UCHAR *ben_enc_rec( BEN *node, UCHAR *p ) {
	ITEM *item = NULL;
	struct obj_tuple *tuple = NULL;
	char buf[MAIN_BUF+1];
	long int len = 0;

	if( node == NULL || p == NULL ) {
		return NULL;
	}
		
	switch( node->t ) {
		case BEN_DICT:
			*p++ = 'd';
 
			if( node->v.d != NULL && node->v.d->item != NULL ) {
				item = list_start( node->v.d );
				do {
					tuple = list_value( item );

					if( tuple->key != NULL && tuple->val != NULL ) {
						if( ( p = ben_enc_rec( tuple->key, p)) == NULL )
							return NULL;
						
						if( ( p = ben_enc_rec( tuple->val, p)) == NULL )
							return NULL;
					}
					
					item = list_next( item );
					
				} while( item != NULL );
			}
			
			*p++ = 'e';
			break;

		case BEN_LIST:
			*p++ = 'l';
			
			if( node->v.l != NULL && node->v.l->item != NULL ) {
				item = list_start( node->v.l );
				do {
					if( ( p = ben_enc_rec( item->val, p)) == NULL )
						return NULL;
					
					item = list_next( item );
					
				} while( item != NULL );
			}

			*p++ = 'e';
			break;

		case BEN_INT:
			snprintf( buf, MAIN_BUF+1, "i%lie", node->v.i );
			len = strlen( buf );
			memcpy( p, buf, len );
			p += len;
			break;

		case BEN_STR:
			/* Meta */
			snprintf( buf, MAIN_BUF+1, "%li:", node->v.s->i );
			len = strlen( buf );
			memcpy( p, buf, len );
			p += len;
			/* Data */
			if( node->v.s->i > 0 ) {
				memcpy( p, node->v.s->s, node->v.s->i );
				p += node->v.s->i;
			}
			break;

	}

	return p;
}

BEN *ben_dec( UCHAR *bencode, long int bensize ) {
	struct obj_raw raw;

	raw.code = (UCHAR *)bencode;
	raw.size = bensize;
	raw.p = (UCHAR *)bencode;

	return ben_dec_r( &raw );
}

BEN *ben_dec_r( struct obj_raw *raw ) {
	BEN *node = NULL;

	switch( *raw->p ) {
		case 'd':
			node = ben_dec_d( raw );
			break;

		case 'l':
			node = ben_dec_l( raw );
			break;

		case 'i':
			node = ben_dec_i( raw );
			break;

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
			node = ben_dec_s( raw );
			break;
	}
	
	return node;
}

BEN *ben_dec_d( struct obj_raw *raw ) {
	BEN *dict = ben_init( BEN_DICT );
	BEN *val = NULL;
	BEN *key = NULL;
	
	raw->p++;
	while( *raw->p != 'e' ) {
		key = ben_dec_s( raw );
		val = ben_dec_r( raw );
		ben_dict( dict, key, val );
	}
	++raw->p;

	return dict;
}

BEN *ben_dec_l( struct obj_raw *raw ) {
	BEN *list = ben_init( BEN_LIST );
	BEN *val = NULL;
	
	raw->p++;
	while( *raw->p != 'e' ) {
		val = ben_dec_r( raw );
		ben_list( list, val );
	}
	++raw->p;
	
	return list;
}

BEN *ben_dec_s( struct obj_raw *raw ) {
	BEN *node = ben_init( BEN_STR );
	long int i = 0;
	long int l = 0;
	UCHAR *start = raw->p;
	UCHAR *buf = NULL;
	int run = 1;

	while( run ) {
		switch( *raw->p ) {
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
				i++;
				raw->p++;
				break;
			case ':':
				buf = (UCHAR *) myalloc( (i+1) * sizeof(UCHAR), "ben_dec_s" );
				memcpy( buf,start,i );
				l = atol( (char *)buf );
				myfree( buf, "ben_dec_s" );
	
				raw->p += 1;
				ben_str( node, raw->p, l );
				raw->p += l;
				
				run = 0;
				break;
		}
	}
   
	return node;
}

BEN *ben_dec_i( struct obj_raw *raw ) {
	BEN *node = ben_init( BEN_INT );
	long int i = 0;
	UCHAR *start = NULL;
	UCHAR *buf = NULL;
	int run = 1;
	long int prefix = 1;
	long int result = 0;

	start = ++raw->p;
	if( *raw->p == '-' ) {
		prefix = -1;
		start = ++raw->p;
	}

	while( run ) {
		switch( *raw->p ) {
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
				i++;
				raw->p++;
				break;
			case 'e':
				buf = (UCHAR *) myalloc( (i+1) * sizeof(UCHAR), "ben_dec_i" );
				memcpy( buf,start,i );
				result = atol( (char *)buf );
				myfree( buf, "ben_dec_i" );

				raw->p++;
				run = 0;
				break;
		}
	}

	result = prefix * result;

	ben_int( node, result );
	
	return node;
}

int ben_validate( UCHAR *bencode, long int bensize ) {
	struct obj_raw raw;

	raw.code = (UCHAR *)bencode;
	raw.size = bensize;
	raw.p = (UCHAR *)bencode;

	return ben_validate_r( &raw );
}

int ben_validate_r( struct obj_raw *raw ) {
	if( raw == NULL )
		return 0;

	if( raw->code == NULL || raw->p == NULL || raw->size < 1 )
		return 0;

	if( ( long int)( raw->p - raw->code) >= raw->size )
		return 0;

	switch( *raw->p ) {
		case 'd':
			if( !ben_validate_d( raw) )
				return 0;
			break;

		case 'l':
			if( !ben_validate_l( raw) )
				return 0;
			break;

		case 'i':
			if( !ben_validate_i( raw) )
				return 0;
			break;

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
			if( !ben_validate_s( raw) )
				return 0;
			break;

		default:
			return 0;
	}

	return 1;
}

int ben_validate_d( struct obj_raw *raw ) {
	if( ( long int)( ++raw->p - raw->code) >= raw->size )
		return 0;
	
	while( *raw->p != 'e' ) {
		if( !ben_validate_s( raw) )
			return 0;
		if( !ben_validate_r( raw) )
			return 0;
		if( ( long int)( raw->p - raw->code) >= raw->size )
			return 0;
	}

	if( ( long int)( ++raw->p - raw->code) > raw->size )
		return 0;
	
	return 1;
}

int ben_validate_l( struct obj_raw *raw ) {
	if( ( long int)( ++raw->p - raw->code) >= raw->size )
		return 0;
	
	while( *raw->p != 'e' ) {
		if( !ben_validate_r( raw) )
			return 0;
		if( ( long int)( raw->p - raw->code) >= raw->size )
			return 0;
	}

	if( ( long int)( ++raw->p - raw->code) > raw->size )
		return 0;
	
	return 1;
}

int ben_validate_s( struct obj_raw *raw ) {
	long int i = 0;
	UCHAR *start = raw->p;
	UCHAR *buf = NULL;
	int run = 1;

	if( ( long int)( raw->p - raw->code) >= raw->size )
		return 0;
	
	while( ( long int)( raw->p - raw->code) < raw->size && run == 1 ) {
		switch( *raw->p ) {
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
				i++;
				raw->p++;
				break;
			case ':':
				/* String length limitation */
				if( i <= 0 || i > BEN_STR_MAXLEN )
					return 0;

				buf = (UCHAR *) myalloc( (i+1) * sizeof(UCHAR), "ben_validate_s" );
				memcpy( buf,start,i );
				i = atol( (char *)buf );
				myfree( buf, "ben_validate_s" );

				/* i < 0 makes no sense */
				if( i < 0 || i > BEN_STR_MAXSIZE )
					return 0;

				raw->p += i+1;
				run = 0;
				break;
			default:
				return 0;
		}
	}

	if( ( long int)( raw->p - raw->code) > raw->size )
		return 0;
	
	return 1;
}

int ben_validate_i( struct obj_raw *raw ) {
	long int i = 0;
	UCHAR *start = NULL;
	UCHAR *buf = NULL;
	int run = 1;
	long int result = 0;

	if( ( long int)( ++raw->p - raw->code) >= raw->size )
		return 0;
	
	start = raw->p;
	if( *raw->p == '-' ) {
		start = ++raw->p;
		
		if( ( long int)( raw->p - raw->code) >= raw->size )
			return 0;
	}

	while( ( long int)( raw->p - raw->code) < raw->size && run == 1 ) {
		switch( *raw->p ) {
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
				i++;
				raw->p++;
				break;
			case 'e':
				if( i <= 0 || i > BEN_INT_MAXLEN )
					return 0;

				buf = (UCHAR *) myalloc( (i+1) * sizeof(UCHAR), "ben_validate_i" );
				memcpy( buf, start, i );
				result = atol( (char *)buf );
				myfree( buf, "ben_validate_i" );

				if( result < 0 || result > BEN_INT_MAXSIZE )
					return 0;

				raw->p++;
				run = 0;
				break;
			default:
				return 0;
		}
	}

	if( (long int)( raw->p - raw->code) > raw->size )
		return 0;
   
	return 1;
}

int ben_is_dict( BEN *node) {
	if( node == NULL ) {
		return 0;
	}

	if( node->t != BEN_DICT ) {
		return 0;
	}

	return 1;
}

int ben_is_list( BEN *node ) {
	if( node == NULL ) {
		return 0;
	}
	
	if( node->t != BEN_LIST ) {
		return 0;
	}

	return 1;
}

int ben_is_str( BEN *node ) {
	if( node == NULL ) {
		return 0;
	}

	if( node->t != BEN_STR ) {
		return 0;
	}

	return 1;
}

int ben_is_int( BEN *node ) {
	if( node == NULL ) {
		return 0;
	}

	if( node->t != BEN_INT ) {
		return 0;
	}

	return 1;
}

BEN *ben_searchDictKey( BEN *node, BEN *key ) {
	ITEM *item = NULL;
	BEN *thiskey = NULL;
	struct obj_tuple *tuple = NULL;
	
	/* Tests */
	if( node == NULL )
		return NULL;
	if( node->t != BEN_DICT )
		return NULL;
	if( key == NULL )
		return NULL;
	if( key->t != BEN_STR )
		return NULL;
	if( node->v.d == NULL )
		return NULL;
	if( node->v.d->item == NULL )
		return NULL;

	item = list_start( node->v.d );

	do {
		tuple = list_value( item );
		thiskey = tuple->key;
		if( thiskey->v.s->i == key->v.s->i && memcmp( thiskey->v.s->s, key->v.s->s, key->v.s->i) == 0 ) {
			return tuple->val;
		}
		item = list_next( item );
		
	} while( item != NULL );

	return NULL;
}

BEN *ben_searchDictStr( BEN *node, const char *buffer ) {
	BEN *result = NULL;
	BEN *key = ben_init( BEN_STR );
	ben_str( key,( UCHAR *)buffer, strlen( buffer) );
	result = ben_searchDictKey( node, key );
	ben_free( key );
	return result;
}

int ben_str_compare( BEN *key1, BEN *key2 ) {
	long int size = 0;
	long int i = 0;

	if( !ben_is_str( key1 ) ) {
		return -1;
	}

	if( !ben_is_str( key2 ) ) {
		return 1;
	}

	size = (key1->v.s->i > key2->v.s->i) ? key1->v.s->i : key2->v.s->i;

	for( i=0; i<size; i++ ) {
		if( key1->v.s->s[i] > key2->v.s->s[i] ) {
			return 1;
		} else if( key1->v.s->s[i] < key2->v.s->s[i] ) {
			return -1;
		}
	}

	/* Strings are equal for the first $size characters */
	if( key1->v.s->i > key2->v.s->i ) {
		return 1;
	} else if( key1->v.s->i < key2->v.s->i ) {
		return -1;
	}

	/* Equal */
	return 0;
}

long int ben_str_size( BEN *node ) {
	if( ! ben_is_str( node ) ) {
		return 0;
	}

	return node->v.s->i;
}

//void ben_sort( BEN *node ) {
//	ITEM *item = NULL;
//	ITEM *next = NULL;
//	BEN *key1 = NULL;
//	BEN *key2 = NULL;
//	struct obj_tuple *tuple_this = NULL;
//	struct obj_tuple *tuple_next = NULL;
//	long int switchcounter = 0;
//	int result = 0;
//	
//	if( node == NULL ) {
//		fail( "ben_sort( 1 )" );
//	}
//	if( node->t != BEN_DICT ) {
//		fail( "ben_sort( 2 )" );
//	}
//	if( node->v.d == NULL ) {
//		return;
//	}
//	if( node->v.d->counter < 2 ) {
//		return;
//	}
//
//	item = node->v.d->start;
//
//	while( 1 ) {
//		next = list_next( item );
//
//		/* Reached the end */
//		if( next == node->v.d->start ) {
//			if( switchcounter == 0 ) {
//				/* The list is sorted now */
//				break;
//			}
//
//			/* Reset switchcounter ... */
//			switchcounter = 0;
//
//			/* ... and start again */
//			item = node->v.d->start;
//			next = list_next( item );
//		}
//
//		tuple_this = list_value( item );
//		tuple_next = list_value( next );
//		key1 = tuple_this->key;
//		key2 = tuple_next->key;
//
//		result = ben_str_compare( key1, key2 );
//		if( result > 0 ) {
//			list_swap( node->v.d, item, next );
//			switchcounter++;
//			
//			/* Continue moving up until start is reached */
//			if( next != node->v.d->start ) {
//				item = list_prev( next );
//			}
//		} else {
//			/* Move down */
//			item = next;
//		}
//	}
//}

struct obj_str *str_init( UCHAR *buf, long int len ) {
	struct obj_str *str = (struct obj_str *) myalloc( sizeof(struct obj_str), "str_init" );
	
	str->s = (UCHAR *) myalloc( (len+1) * sizeof(UCHAR), "str_init" );
	if( len > 0 ) {
		memcpy( str->s, buf, len );
	}
	str->i = len;
	
	return str;
}

void str_free( struct obj_str *str ) {
	if( str->s != NULL ) {
		myfree( str->s, "str_free" );
	}
	myfree( str, "str_free" );
}
