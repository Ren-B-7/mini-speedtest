/*******************************************************************************
***
***     Author: Tyler Barrus
***     email:  barrust@gmail.com
***
***     Version: 0.2.0
***
***     License: MIT 2016
***
*******************************************************************************/

#include "set.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FULLNESS_RATIO 0.25 /* arbitrary */

/* PRIVATE FUNCTIONS */
static uint64_t set_default_hash(const char* key, key_size_tt len);
static int set_get_index(uint64_t hash, const SimpleSet* set, const char* key,
 key_size_tt len, uint64_t* index);
static int set_assign_node(uint64_t hash, SimpleSet* set, uint64_t index,
 const char* key, key_size_tt len);
static void set_free_index(SimpleSet* set, uint64_t index);
static int set_contains_hash(uint64_t hash, const SimpleSet* set,
 const char* key, key_size_tt len);
static int
set_add_hash(uint64_t hash, SimpleSet* set, const char* key, key_size_tt len);
static int
set_relayout_nodes(uint64_t start, SimpleSet* set, short end_on_null);

/*******************************************************************************
***        FUNCTIONS DEFINITIONS
*******************************************************************************/

int set_init_alt(SimpleSet* set, uint64_t num_els, set_hash_function hash)
{
    set->nodes = (simple_set_node**)malloc(num_els * sizeof(simple_set_node*));
    if (set->nodes == NULL) {
        return SET_MALLOC_ERROR;
    }
    set->number_nodes = num_els;
    uint64_t i;
    for (i = 0; i < set->number_nodes; ++i) {
        set->nodes[i] = NULL;
    }
    set->used_nodes = 0;
    set->hash_function = (hash == NULL) ? &set_default_hash : hash;
    return SET_TRUE;
}

int set_clear(SimpleSet* set)
{
    uint64_t i;
    for (i = 0; i < set->number_nodes; ++i) {
        if (set->nodes[i] != NULL) {
            set_free_index(set, i);
        }
    }
    set->used_nodes = 0;
    return SET_TRUE;
}

int set_destroy(SimpleSet* set)
{
    set_clear(set);
    free((void*)set->nodes);
    set->number_nodes = 0;
    set->used_nodes = 0;
    set->hash_function = NULL;
    return SET_TRUE;
}

int set_add(SimpleSet* set, const char* key, key_size_tt len)
{
    uint64_t hash = set->hash_function(key, len);
    return set_add_hash(hash, set, key, len);
}

int set_add_str(SimpleSet* set, const char* key)
{
    return set_add(set, key, strlen(key));
}

int set_contains(const SimpleSet* set, const char* key, key_size_tt len)
{
    uint64_t index, hash = set->hash_function(key, len);
    return set_get_index(hash, set, key, len, &index);
}

int set_contains_str(const SimpleSet* set, const char* key)
{
    return set_contains(set, key, strlen(key));
}

int set_remove(SimpleSet* set, const char* key, key_size_tt len)
{
    uint64_t index, hash = set->hash_function(key, len);
    int pos = set_get_index(hash, set, key, len, &index);
    if (pos != SET_TRUE) {
        return pos;
    }
    // remove this node
    set_free_index(set, index);
    // re-layout nodes
    set_relayout_nodes(index, set, 0);
    --set->used_nodes;
    return SET_TRUE;
}

int set_remove_str(SimpleSet* set, const char* key)
{
    return set_remove(set, key, strlen(key));
}

uint64_t set_length(const SimpleSet* set)
{
    return set->used_nodes;
}

char** set_to_array(const SimpleSet* set, uint64_t* size)
{
    *size = set->used_nodes;
    char** results = (char**)calloc(set->used_nodes + 1, sizeof(char*));
    uint64_t i, j = 0;
    for (i = 0; i < set->number_nodes; ++i) {
        if (set->nodes[i] != NULL) {
            size_t len = set->nodes[i]->_len;
            results[j] = (char*)calloc(len + 1, sizeof(char));
            memcpy(results[j], set->nodes[i]->_key, len);
            ++j;
        }
    }
    return results;
}

int set_union(SimpleSet* res, const SimpleSet* s1, const SimpleSet* s2)
{
    if (res->used_nodes != 0) {
        return SET_OCCUPIED_ERROR;
    }
    // loop over both s1 and s2 and get keys and insert them into res
    uint64_t i;
    for (i = 0; i < s1->number_nodes; ++i) {
        if (s1->nodes[i] != NULL) {
            set_add_hash(s1->nodes[i]->_hash, res, s1->nodes[i]->_key,
             s1->nodes[i]->_len);
        }
    }
    for (i = 0; i < s2->number_nodes; ++i) {
        if (s2->nodes[i] != NULL) {
            set_add_hash(s2->nodes[i]->_hash, res, s2->nodes[i]->_key,
             s2->nodes[i]->_len);
        }
    }
    return SET_TRUE;
}

int set_intersection(SimpleSet* res, const SimpleSet* s1, const SimpleSet* s2)
{
    if (res->used_nodes != 0) {
        return SET_OCCUPIED_ERROR;
    }
    // loop over both one of s1 and s2: get keys, check the other, and insert
    // them into res if it is
    uint64_t i;
    for (i = 0; i < s1->number_nodes; ++i) {
        if (s1->nodes[i] != NULL) {
            if (set_contains_hash(s1->nodes[i]->_hash, s2, s1->nodes[i]->_key,
                 s1->nodes[i]->_len) == SET_TRUE) {
                set_add_hash(s1->nodes[i]->_hash, res, s1->nodes[i]->_key,
                 s1->nodes[i]->_len);
            }
        }
    }
    return SET_TRUE;
}

/* difference is s1 - s2 */
int set_difference(SimpleSet* res, const SimpleSet* s1, const SimpleSet* s2)
{
    if (res->used_nodes != 0) {
        return SET_OCCUPIED_ERROR;
    }
    // loop over s1 and keep only things not in s2
    uint64_t i;
    for (i = 0; i < s1->number_nodes; ++i) {
        if (s1->nodes[i] != NULL) {
            if (set_contains_hash(s1->nodes[i]->_hash, s2, s1->nodes[i]->_key,
                 s1->nodes[i]->_len) != SET_TRUE) {
                set_add_hash(s1->nodes[i]->_hash, res, s1->nodes[i]->_key,
                 s1->nodes[i]->_len);
            }
        }
    }
    return SET_TRUE;
}

int set_symmetric_difference(SimpleSet* res, const SimpleSet* s1,
 const SimpleSet* s2)
{
    if (res->used_nodes != 0) {
        return SET_OCCUPIED_ERROR;
    }
    uint64_t i;
    // loop over set 1 and add elements that are unique to set 1
    for (i = 0; i < s1->number_nodes; ++i) {
        if (s1->nodes[i] != NULL) {
            if (set_contains_hash(s1->nodes[i]->_hash, s2, s1->nodes[i]->_key,
                 s1->nodes[i]->_len) != SET_TRUE) {
                set_add_hash(s1->nodes[i]->_hash, res, s1->nodes[i]->_key,
                 s1->nodes[i]->_len);
            }
        }
    }
    // loop over set 2 and add elements that are unique to set 2
    for (i = 0; i < s2->number_nodes; ++i) {
        if (s2->nodes[i] != NULL) {
            if (set_contains_hash(s2->nodes[i]->_hash, s1, s2->nodes[i]->_key,
                 s2->nodes[i]->_len) != SET_TRUE) {
                set_add_hash(s2->nodes[i]->_hash, res, s2->nodes[i]->_key,
                 s2->nodes[i]->_len);
            }
        }
    }
    return SET_TRUE;
}

int set_is_subset(const SimpleSet* test, const SimpleSet* against)
{
    uint64_t i;
    for (i = 0; i < test->number_nodes; ++i) {
        if (test->nodes[i] != NULL) {
            if (set_contains_hash(test->nodes[i]->_hash, against,
                 test->nodes[i]->_key, test->nodes[i]->_len) == SET_FALSE) {
                return SET_FALSE;
            }
        }
    }
    return SET_TRUE;
}

int set_is_subset_strict(const SimpleSet* test, const SimpleSet* against)
{
    if (test->used_nodes >= against->used_nodes) {
        return SET_FALSE;
    }
    return set_is_subset(test, against);
}

int set_cmp(const SimpleSet* left, const SimpleSet* right)
{
    if (left->used_nodes < right->used_nodes) {
        return SET_RIGHT_GREATER;
    } else if (right->used_nodes < left->used_nodes) {
        return SET_LEFT_GREATER;
    }
    uint64_t i;
    for (i = 0; i < left->number_nodes; ++i) {
        if (left->nodes[i] != NULL) {
            if (set_contains(right, left->nodes[i]->_key,
                 left->nodes[i]->_len) != SET_TRUE) {
                return SET_UNEQUAL;
            }
        }
    }

    return SET_EQUAL;
}

/*******************************************************************************
***        PRIVATE FUNCTIONS
*******************************************************************************/
static uint64_t set_default_hash(const char* key, key_size_tt len)
{
    // FNV-1a hash (http://www.isthe.com/chongo/tech/comp/fnv/)
    uint64_t h = 14695981039346656037ULL; // FNV_OFFSET 64 bit
    for (size_t i = 0; i < len; ++i) {
        h = h ^ (unsigned char)key[i];
        h = h * 1099511628211ULL; // FNV_PRIME 64 bit
    }
    return h;
}

static int set_contains_hash(uint64_t hash, const SimpleSet* set,
 const char* key, key_size_tt len)
{
    uint64_t index;
    return set_get_index(hash, set, key, len, &index);
}

static int
set_add_hash(uint64_t hash, SimpleSet* set, const char* key, key_size_tt len)
{
    uint64_t index;
    if (set_contains_hash(hash, set, key, len) == SET_TRUE) {
        return SET_ALREADY_PRESENT;
    }

    // Expand nodes if we are close to our desired fullness
    if (set->used_nodes * 4 > set->number_nodes) {
        uint64_t num_els = set->number_nodes * 2; // we want to double each time
        simple_set_node** tmp = (simple_set_node**)realloc((void*)set->nodes,
         num_els * sizeof(simple_set_node*));
        if (tmp == NULL) { // malloc failure
            return SET_MALLOC_ERROR;
        }

        set->nodes = tmp;
        uint64_t i, orig_num_els = set->number_nodes;
        for (i = orig_num_els; i < num_els; ++i) {
            set->nodes[i] = NULL;
        }

        set->number_nodes = num_els;
        // re-layout all nodes
        set_relayout_nodes(0, set, 1);
    }
    // add element in
    int res = set_get_index(hash, set, key, len, &index);
    if (res == SET_FALSE) { // this is the first open slot
        set_assign_node(hash, set, index, key, len);
        ++set->used_nodes;
        return SET_TRUE;
    }
    return res;
}

static int set_get_index(uint64_t hash, const SimpleSet* set, const char* key,
 key_size_tt len, uint64_t* index)
{
    uint64_t i, idx;
    idx = hash % set->number_nodes;
    i = idx;
    while (1) {
        if (set->nodes[i] == NULL) {
            *index = i;
            return SET_FALSE; // not here OR first open slot
        } else if (hash == set->nodes[i]->_hash && len == set->nodes[i]->_len &&
         memcmp(key, set->nodes[i]->_key, len) == 0) {
            *index = i;
            return SET_TRUE;
        }
        ++i;
        if (i == set->number_nodes) {
            i = 0;
        }
        if (i == idx) { // this means we went all the way around and the set is
                        // full
            return SET_CIRCULAR_ERROR;
        }
    }
}

static int set_assign_node(uint64_t hash, SimpleSet* set, uint64_t index,
 const char* key, key_size_tt len)
{
    set->nodes[index] = (simple_set_node*)malloc(sizeof(simple_set_node));
    set->nodes[index]->_key = (char*)calloc(len + 1, sizeof(char));
    set->nodes[index]->_len = len;
    memcpy(set->nodes[index]->_key, key, len);
    set->nodes[index]->_hash = hash;
    return SET_TRUE;
}

static void set_free_index(SimpleSet* set, uint64_t index)
{
    free(set->nodes[index]->_key);
    free(set->nodes[index]);
    set->nodes[index] = NULL;
}

static int set_relayout_nodes(uint64_t start, SimpleSet* set, short end_on_null)
{
    int moved_one = 1;
    uint64_t index = 0, j;
    for (j = 0; j < set->number_nodes; ++j) {
        uint64_t i = (start + j) % set->number_nodes;
        if (set->nodes[i] != NULL) {
            int res = set_get_index(set->nodes[i]->_hash, set,
             set->nodes[i]->_key, set->nodes[i]->_len, &index);
            if (res == SET_FALSE) {
                moved_one = 0;
                set->nodes[index] = set->nodes[i];
                set->nodes[i] = NULL;
            }
        } else if (end_on_null == 0 && j != 0) {
            break;
        }
    }
    return moved_one;
}
