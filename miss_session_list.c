/*
 * miss_session_list.c
 *
 *  Created on: Aug 15, 2020
 *      Author: ning
 */

/*
 * header
 */
//system header
#include <stdio.h>
//program header

//server header
#include "miss_session_list.h"

/*
 * static
 */
//variable

//function

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */




/*
* Delete a list entry by making the prev/next entries
* point to each other.
*
* This is only for internal list manipulation where we know
* the prev/next entries already!
*/
void __miss_list_del(struct list_handle * prev, struct list_handle * next)
{
	next->prev = prev;
	prev->next = next;
}


/*
* Insert a new entry between two known consecutive entries.
*
* This is only for internal list manipulation where we know
* the prev/next entries already!
*/
void __miss_list_add(struct list_handle *new,
struct list_handle *prev,
struct list_handle *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}


void miss_list_init(struct list_handle *list)
{
	list->next = list;
	list->prev = list;
}
/**
* list_add_tail - add a new entry
* @new: new entry to be added
* @head: list head to add it before
*
* Insert a new entry before the specified head.
* This is useful for implementing queues.
*/
void miss_list_add_tail(struct list_handle *new, struct list_handle *head)
{
	__miss_list_add(new, head->prev, head);
}

void miss_list_add_head(struct list_handle *new, struct list_handle *head)
{
	__miss_list_add(new, head, head->next);
}

/**
* list_del - deletes entry from list.
* @entry: the element to delete from the list.
* Note: list_empty() on entry does not return true after this, the entry is
* in an undefined state.
*/
void miss_list_del(struct list_handle *entry)
{
	__miss_list_del(entry->prev, entry->next);
	entry->next = 0;
	entry->prev = 0;
}

/**
* list_empty - tests whether a list is empty
* @head: the list to test.
*/
int miss_list_is_empty(const struct list_handle *head)
{
	return head->next == head;
}
