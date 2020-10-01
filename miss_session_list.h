/*
 * miss_session_list.h
 *
 *  Created on: Aug 15, 2020
 *      Author: ning
 */

#ifndef SERVER_MISS_MISS_SESSION_LIST_H_
#define SERVER_MISS_MISS_SESSION_LIST_H_

/*
 * header
 */

/*
 * define
 */

/*
 * structure
 */
struct list_handle
{
	struct list_handle *next, *prev;
};

/*
 * function
 */

#ifndef offsetof
	#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define container_of(ptr, type, member) ({			\
	const typeof(((type *)0)->member) * __mptr = (ptr);	\
	(type *)((char *)__mptr - offsetof(type, member)); })

/**
 * list_entry - get the struct for this entry
 * @ptr:	the &struct list_handle pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the list_struct within the struct.
 */
#define list_entry(ptr, type, member)			container_of(ptr, type, member)

/**
 * list_first_entry - get the first element from a list
 * @ptr:	the list head to take the element from.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the list_struct within the struct.
 *
 * Note, that list is expected to be not empty.
 */
#define list_first_entry(ptr, type, member)  	list_entry((ptr)->next, type, member)


/**
 * list_for_each	-	iterate over a list
 * @pos:	the &struct list_handle to use as a loop cursor.
 * @head:	the head for your list.
 */
#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_prev(pos, head) \
	for (pos = (head)->prev;  pos != (head); pos = pos->prev)



#endif /* SERVER_MISS_MISS_SESSION_LIST_H_ */
