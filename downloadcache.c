/*  This file is part of "mirrorer" (TODO: find better title)
 *  Copyright (C) 2004 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <config.h>

#include <assert.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include "error.h"
#include "strlist.h"
#include "names.h"
#include "dirs.h"
#include "files.h"
#include "downloadcache.h"


struct downloaditem {
	struct downloaditem *parent,*left,*right;
	struct tobedone *todo;
};

struct downloadcache {
	struct downloaditem *items;
};

/* Initialize a new download session */
retvalue downloadcache_initialize(struct downloadcache **download) {
	struct downloadcache *cache;

	cache = malloc(sizeof(struct downloadcache));
	if( cache == NULL )
		return RET_ERROR_OOM;
	cache->items = NULL;
	*download = cache;
	return RET_OK;
}

/* free all memory */
static void freeitem(struct downloaditem *item) {
	if( item == NULL )
		return;
	freeitem(item->left);
	freeitem(item->right);
	free(item);
}
retvalue downloadcache_free(struct downloadcache *download) {

	if( download == NULL )
		return RET_NOTHING;

	freeitem(download->items);
	free(download);
	return RET_OK;
}

const struct downloaditem *searchforitem(struct downloadcache *list,
					const char *filekey,
					struct downloaditem **p,
					struct downloaditem ***h) {
	struct downloaditem *item;
	int c;

	item = list->items;
	while( item ) {
		*p = item;
		c = strcmp(filekey,item->todo->filekey);
		if( c == 0 )
			return item;
		else if( c < 0 ) {
			*h = &item->left;
			item = item->left;
		} else {
			*h = &item->right;
			item = item->right;
		}
	}
	return NULL;
}

/* queue a new file to be downloaded: 
 * results in RET_ERROR_WRONG_MD5, if someone else already asked
 * for the same destination with other md5sum created. */
retvalue downloadcache_add(struct downloadcache *cache,filesdb filesdb,struct aptmethod *method,const char *orig,const char *filekey,const char *md5sum) {

	const struct downloaditem *i;
	struct downloaditem *item,**h,*parent;
	char *fullfilename;
	retvalue r;

	assert(filesdb && cache && method);
	r = files_check(filesdb,filekey,md5sum);
	if( r != RET_NOTHING )
		return r;

	i = searchforitem(cache,filekey,&parent,&h);
	if( i != NULL ) {
		if( strcmp(md5sum,i->todo->md5sum) == 0 )
			return RET_NOTHING;
		// TODO: print error;
		return RET_ERROR_WRONG_MD5;
	}
	item = malloc(sizeof(struct downloaditem));
	if( item == NULL )
		return RET_ERROR_OOM;

	fullfilename = calc_dirconcat(filesdb->mirrordir,filekey);
	if( fullfilename == NULL ) {
		free(item);
		return RET_ERROR_OOM;
	}
	(void)dirs_make_parent(fullfilename);
	r = aptmethod_queuefile(method,orig,fullfilename,md5sum,filekey,&item->todo);
	free(fullfilename);
	if( RET_WAS_ERROR(r) ) {
		free(item);
		return r;
	}
	item->left = item->right = NULL;

	if( cache->items == NULL ) {
		cache->items = item;
		item->parent = NULL;
		return RET_OK;
	}
	item->parent = parent;
	*h = item;

	return RET_OK;
}

/* some as above, only for more files... */
retvalue downloadcache_addfiles(struct downloadcache *cache,filesdb filesdb,
		struct aptmethod *method,
		const struct strlist *origfiles,
		const struct strlist *filekeys,
		const struct strlist *md5sums) {
	retvalue result,r;
	int i;

	assert(origfiles && filekeys && md5sums
		&& origfiles->count == filekeys->count
		&& md5sums->count == filekeys->count);

	result = RET_NOTHING;
	
	for( i = 0 ; i < filekeys->count ; i++ ) {
		r = downloadcache_add(cache,filesdb,method,
			origfiles->values[i],
			filekeys->values[i],
			md5sums->values[i]);
		RET_UPDATE(result,r);
	}
	return result;
}