#pragma once
#include <stdbool.h>

// Themes kept in the one theme package (see theme_stack.c).

/** {"themes":[{"id","name"}…],"pages":N} from the installed manifest (malloc'd, free() it). Never NULL unless out of memory. */
char *theme_stack_list_json(void);

/** Removes the theme that owns page_id (all its pages) from the manifest; erases the manifest when nothing is left.
 *  *left gets the number of theme pages that remain (-1 when nothing was read). */
bool theme_stack_remove_page_owner(const char *page_id, int *left);
