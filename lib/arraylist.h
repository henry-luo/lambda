/*

Copyright (c) 2005-2008, Simon Howard

Permission to use, copy, modify, and/or distribute this software 
for any purpose with or without fee is hereby granted, provided 
that the above copyright notice and this permission notice appear 
in all copies. 

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL 
WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE 
AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR 
CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM 
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, 
NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN      
CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. 

 */

/** 
 * @file arraylist.h
 *
 * @brief Automatically resizing array 
 *
 * ArrayLists are arrays of pointers which automatically increase in 
 * size.
 *
 * To create an ArrayList, use @ref arraylist_new.
 * To destroy an ArrayList, use @ref arraylist_free.
 *
 * To add a value to an ArrayList, use @ref arraylist_prepend, 
 * @ref arraylist_append, or @ref arraylist_insert.
 *
 * To remove a value from an ArrayList, use @ref arraylist_remove
 * or @ref arraylist_remove_range.
 */

#ifndef ALGORITHM_ARRAYLIST_H
#define ALGORITHM_ARRAYLIST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A value to be stored in an @ref ArrayList.
 */

typedef void *ArrayListValue;

/**
 * An ArrayList structure.  New ArrayLists can be created using the 
 * arraylist_new function.
 *
 * @see arraylist_new 
 */

typedef struct _ArrayList ArrayList;

/**
 * Definition of an @ref ArrayList.
 */ 

#ifndef _ARRAYLIST_STRUCT_DEFINED
#define _ARRAYLIST_STRUCT_DEFINED
struct _ArrayList {

	/** Entries in the array */
	
	ArrayListValue *data;

	/** Length of the array */
	
	int length;

	/** Private data and should not be accessed */

	int _alloced;
};
#endif

/**
 * Compare two values in an arraylist to determine if they are equal.
 *
 * @return Non-zero if the values are not equal, zero if they are equal.
 */

typedef int (*ArrayListEqualFunc)(ArrayListValue value1, ArrayListValue value2);

/**
 * Compare two values in an arraylist.  Used by @ref arraylist_sort
 * when sorting values.
 *
 * @param value1              The first value.
 * @param value2              The second value.
 * @return                    A negative number if value1 should be sorted
 *                            before value2, a positive number if value2 should
 *                            be sorted before value1, zero if the two values
 *                            are equal.
 */

typedef int (*ArrayListCompareFunc)(ArrayListValue value1,
                                    ArrayListValue value2);

/**
 * Allocate a new ArrayList for use.
 *
 * @param length         Hint to the initialise function as to the amount
 *                       of memory to allocate initially to the ArrayList.
 * @return               A new arraylist, or NULL if it was not possible
 *                       to allocate the memory.
 * @see arraylist_free
 */

ArrayList *arraylist_new(int length);

/**
 * Destroy an ArrayList and free back the memory it uses.
 *
 * @param arraylist      The ArrayList to free.
 */

void arraylist_free(ArrayList *arraylist);

/**
 * Append a value to the end of an ArrayList.
 *
 * @param arraylist      The ArrayList.
 * @param data           The value to append.
 * @return               Non-zero if the request was successful, zero
 *                       if it was not possible to allocate more memory
 *                       for the new entry.
 */

int arraylist_append(ArrayList *arraylist, ArrayListValue data);

/** 
 * Prepend a value to the beginning of an ArrayList.
 *
 * @param arraylist      The ArrayList.
 * @param data           The value to prepend.
 * @return               Non-zero if the request was successful, zero
 *                       if it was not possible to allocate more memory
 *                       for the new entry.
 */

int arraylist_prepend(ArrayList *arraylist, ArrayListValue data);

/**
 * Remove the entry at the specified location in an ArrayList.
 *
 * @param arraylist      The ArrayList.
 * @param index          The index of the entry to remove.
 */

void arraylist_remove(ArrayList *arraylist, int index);

/**
 * Remove a range of entries at the specified location in an ArrayList.
 *
 * @param arraylist      The ArrayList.
 * @param index          The index of the start of the range to remove.
 * @param length         The length of the range to remove.
 */

void arraylist_remove_range(ArrayList *arraylist, int index, int length);

/**
 * Insert a value at the specified index in an ArrayList.
 * The index where the new value can be inserted is limited by the 
 * size of the ArrayList.
 *
 * @param arraylist      The ArrayList.
 * @param index          The index at which to insert the value.
 * @param data           The value.
 * @return               Returns zero if unsuccessful, else non-zero 
 *                       if successful (due to an invalid index or 
 *                       if it was impossible to allocate more memory).
 */

int arraylist_insert(ArrayList *arraylist, int index, ArrayListValue data);

/**
 * Find the index of a particular value in an ArrayList.
 *
 * @param arraylist      The ArrayList to search.
 * @param callback       Callback function to be invoked to compare
 *                       values in the list with the value to be
 *                       searched for.
 * @param data           The value to search for.
 * @return               The index of the value if found, or -1 if not found.
 */

int arraylist_index_of(ArrayList *arraylist, 
                       ArrayListEqualFunc callback, 
                       ArrayListValue data);

/** 
 * Remove all entries from an ArrayList.
 *
 * @param arraylist      The ArrayList.
 */

void arraylist_clear(ArrayList *arraylist);

/** 
 * Sort the values in an ArrayList.
 *
 * @param arraylist      The ArrayList.
 * @param compare_func   Function used to compare values in sorting.
 */

void arraylist_sort(ArrayList *arraylist, ArrayListCompareFunc compare_func);

/* Accessors. Inline so they compile down to a direct pointer/length read. */

static inline ArrayListValue arraylist_get(const ArrayList *arraylist, int index) {
	return arraylist->data[index];
}

static inline int arraylist_size(const ArrayList *arraylist) {
	return arraylist->length;
}

static inline int arraylist_length(const ArrayList *arraylist) {
	return arraylist->length;
}

static inline ArrayListValue arraylist_front(const ArrayList *arraylist) {
	return arraylist->data[0];
}

static inline ArrayListValue arraylist_back(const ArrayList *arraylist) {
	return arraylist->data[arraylist->length - 1];
}

static inline void arraylist_set(ArrayList *arraylist, int index, ArrayListValue value) {
	arraylist->data[index] = value;
}

/* Remove and return the last element. Returns NULL if the list is empty. */
ArrayListValue arraylist_pop(ArrayList *arraylist);

/* Remove and return the first element. Returns NULL if the list is empty. */
ArrayListValue arraylist_pop_front(ArrayList *arraylist);

/* Ensure capacity for at least `capacity` entries. Returns non-zero on success. */
int arraylist_reserve(ArrayList *arraylist, int capacity);

/* Iterate over each entry. `var` is bound to the cast-to-type value. */
#define ARRAYLIST_FOREACH(list, type, var) \
	for (int _al_i = 0, _al_once = 1; _al_i < (list)->length; ++_al_i, _al_once = 1) \
		for (type var = (type)(list)->data[_al_i]; _al_once; _al_once = 0)

#ifdef __cplusplus
}
#endif

#endif /* #ifndef ALGORITHM_ARRAYLIST_H */

