/*
    path.c -- path functions needed for tree lookup.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>
#include <reiserfs/path.h>

reiserfs_path_t *reiserfs_path_create(void *data) {
    reiserfs_path_t *path;

    if (!(path = aal_calloc(sizeof(*path), 0)))
	return NULL;
    
    path->data = data;
    return path;
}

void reiserfs_path_free(reiserfs_path_t *path) {
    
    aal_assert("umka-460", path != NULL, return);
    
    if (aal_list_length(path->entity))
	aal_list_free(path->entity);
    
    path->entity = NULL;
    aal_free(path);
}

reiserfs_coord_t *reiserfs_path_append(reiserfs_path_t *path, 
    reiserfs_coord_t *coord)
{
    aal_assert("umka-461", path != NULL, return NULL);
    aal_assert("umka-462", coord != NULL, return NULL);

    if (!aal_list_append(path->entity, (void *)coord))
	return NULL;
    
    return coord;
}

reiserfs_coord_t *reiserfs_path_insert(reiserfs_path_t *path, 
    reiserfs_coord_t *coord, uint8_t level)
{
    aal_assert("umka-463", path != NULL, return NULL);
    aal_assert("umka-464", coord != NULL, return NULL);

    if (!aal_list_insert(path->entity, coord, level))
	return NULL;
    
    return coord;
}

void reiserfs_path_remove(reiserfs_path_t *path, 
    reiserfs_coord_t *coord)
{
    aal_assert("umka-465", path != NULL, return);
    aal_assert("umka-466", coord != NULL, return);
    
    reiserfs_node_close(coord->node);
    aal_free(coord);
    aal_list_remove(path->entity, coord);
}

void reiserfs_path_delete(reiserfs_path_t *path, 
    uint8_t level)
{
    aal_assert("umka-467", path != NULL, return);
    reiserfs_path_remove(path, (reiserfs_coord_t *)aal_list_at(path->entity, level));
}

reiserfs_coord_t *reiserfs_path_at(reiserfs_path_t *path, 
    uint8_t level)
{
    aal_assert("umka-469", path != NULL, return NULL);
    return (reiserfs_coord_t *)aal_list_at(path->entity, level);
}

void reiserfs_path_clear(reiserfs_path_t *path) {
    aal_list_t *walk;
    
    aal_assert("umka-477", path != NULL, return);
    
    for (walk = aal_list_last(path->entity); walk; ) {
	aal_list_t *temp = aal_list_prev(walk);
	reiserfs_path_remove(path, temp->data);
	walk = temp;
    }
    path->entity = NULL;
}

