/* nbdkit
 * Copyright Red Hat
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of Red Hat nor the names of its contributors may be
 * used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Simple implementation of a vector.
 *
 * Appending to the end is cheap.  Inserting in the middle is more
 * expensive.  The vector can be a list of anything, eg. ints,
 * structs, pointers.
 *
 * The vector is implemented as a struct with three fields:
 *
 *   struct <name> {
 *     <type> *ptr;    Pointer to array of items.
 *     size_t len;     Number of valid items (ptr[0] .. ptr[len-1]).
 *     size_t cap;     Capacity, size of the array allocated.
 *   };
 *   typedef struct <name> <name>;
 *
 * Note you can just access the struct fields directly.  That is
 * intentional, they are not private!
 *
 * When defining a vector type you give the <name> to use for the new
 * vector type, and the <type> of each element:
 *
 *   DEFINE_VECTOR_TYPE (<name>, <type>);
 *
 * This also defines functions called "<name>_reserve",
 * "<name>_append", etc.
 *
 * <name>_reserve (&vector, n) reserves at least n additional elements
 * beyond the current capacity.  This is a wrapper around "realloc"
 * and might fail (returning -1).  If it succeeds (returning 0) then
 * you are allowed to call <name>_append (&vector, elem) up to n times
 * and it will not fail.
 *
 * <name>_append (&vector, elem) appends the new element at the end of
 * the array (at ptr[len]), increasing the length.  This may need to
 * reallocate the array if there is not sufficient capacity.
 *
 * <name>_insert and <name>_remove insert and remove single elements
 * in the middle of the array.  <name>_insert may have to extend the
 * array, so it may fail, while <name>_remove can never fail.
 *
 * There are various other methods, see the macros below.
 *
 * For example, you could define a list of ints as:
 *
 *   DEFINE_VECTOR_TYPE (int_vector, int);
 *   int_vector myints = empty_vector;
 *
 * where "myints.ptr[]" will be an array of ints and "myints.len" will
 * be the number of ints.  There are no get/set accessors.  To iterate
 * over myints you can use the ".ptr" field directly:
 *
 *   for (size_t i = 0; i < myints.len; ++i)
 *     printf ("%d\n", myints.ptr[i]);
 *
 * Initializing with "empty_vector", or assigning the compound literal
 * "(int_vector)empty_vector", sets .ptr = NULL and .len = 0
 *
 * Because the implementation uses realloc, the .ptr array may move,
 * so you should not save the address of array elements.
 *
 * There are predefined types in "string-vector.h" and
 * "const-string-vector.h" for storing lists of strings.
 */

#ifndef NBDKIT_VECTOR_H
#define NBDKIT_VECTOR_H

#include <stdbool.h>
#include <assert.h>
#include <string.h>

#include "compiler-macros.h"
#include "static-assert.h"

#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wduplicate-decl-specifier"
#endif

#define DEFINE_VECTOR_TYPE(name, type)                                  \
  struct name {                                                         \
    type *ptr;              /* Pointer to array of items. */            \
    size_t len;             /* Number of valid items in the array. */   \
    size_t cap;             /* Maximum number of items. */              \
  };                                                                    \
  typedef struct name name;                                             \
                                                                        \
  /* Reserve at least n elements at the end of the vector.  Note        \
   * space is allocated and capacity is increased, but the vector       \
   * length is not increased and the new elements are not initialized.  \
   */                                                                   \
  static inline int __attribute__ ((__unused__))                        \
  name##_reserve (name *v, size_t n)                                    \
  {                                                                     \
    return generic_vector_reserve ((struct generic_vector *)v, n,       \
                                   sizeof (type), false);               \
  }                                                                     \
                                                                        \
  /* Same as _reserve, but reserve exactly this number of elements      \
   * without any overhead.  Useful if you know ahead of time that you   \
   * will never need to extend the vector.                              \
   */                                                                   \
  static inline int __attribute__ ((__unused__))                        \
  name##_reserve_exactly (name *v, size_t n)                            \
  {                                                                     \
    return generic_vector_reserve ((struct generic_vector *)v,          \
                                   n, sizeof (type), true);             \
  }                                                                     \
                                                                        \
  /* Same as _reserve, but the allocation will be page aligned.  Note   \
   * that the machine page size must be divisible by sizeof (type).     \
   */                                                                   \
  static inline int __attribute__ ((__unused__))                        \
  name##_reserve_page_aligned (name *v, size_t n)                       \
  {                                                                     \
    return generic_vector_reserve_page_aligned ((struct generic_vector *)v, \
                                                n, sizeof (type));      \
  }                                                                     \
                                                                        \
  /* Insert list at i'th element.  i=0 => beginning  i=len => append */ \
  static inline int __attribute__ ((__unused__))                        \
  name##_insert_array (name *v, type *elems, size_t nr_elems, size_t i) \
  {                                                                     \
    assert (i <= v->len);                                               \
    if (v->len+nr_elems > v->cap) {                                     \
      if (name##_reserve (v, nr_elems) == -1) return -1;                \
    }                                                                   \
    memmove (&v->ptr[i+nr_elems], &v->ptr[i],                           \
             (v->len-i) * sizeof (elems[0]));                           \
    memcpy (&v->ptr[i], elems, nr_elems * sizeof (elems[0]));           \
    v->len += nr_elems;                                                 \
    return 0;                                                           \
  }                                                                     \
                                                                        \
  /* Insert at i'th element.  i=0 => beginning  i=len => append */      \
  static inline int __attribute__ ((__unused__))                        \
  name##_insert (name *v, type elem, size_t i)                          \
  {                                                                     \
    return name##_insert_array (v, &elem, 1, i);                        \
  }                                                                     \
                                                                        \
  /* Append a new element to the end of the vector. */                  \
  static inline int __attribute__ ((__unused__))                        \
  name##_append (name *v, type elem)                                    \
  {                                                                     \
    return name##_insert (v, elem, v->len);                             \
  }                                                                     \
                                                                        \
  /* Append list of new elements to the end of the vector. */           \
  static inline int __attribute__ ((__unused__))                        \
  name##_append_array (name *v, type *elems, size_t nr_elems)           \
  {                                                                     \
    return name##_insert_array (v, elems, nr_elems, v->len);            \
  }                                                                     \
                                                                        \
  /* Remove [i..i+nr-1] elements.  i=0 => beginning  i=len-1 => end */  \
  static inline void __attribute__ ((__unused__))                       \
  name##_remove_range (name *v, size_t nr, size_t i)                    \
  {                                                                     \
    assert (i < v->len);                                                \
    memmove (&v->ptr[i], &v->ptr[i+nr], (v->len-i-nr) * sizeof (type)); \
    v->len -= nr;                                                       \
  }                                                                     \
                                                                        \
  /* Remove i'th element.  i=0 => beginning  i=len-1 => end */          \
  static inline void __attribute__ ((__unused__))                       \
  name##_remove (name *v, size_t i)                                     \
  {                                                                     \
    name##_remove_range (v, 1, i);                                      \
  }                                                                     \
                                                                        \
  /* Remove all elements and deallocate the vector. */                  \
  static inline void __attribute__ ((__unused__))                       \
  name##_reset (name *v)                                                \
  {                                                                     \
    free (v->ptr);                                                      \
    v->ptr = NULL;                                                      \
    v->len = v->cap = 0;                                                \
  }                                                                     \
                                                                        \
  /* Iterate over the vector, calling f() on each element. */           \
  static inline void __attribute__ ((__unused__))                       \
  name##_iter (name *v, void (*f) (type elem))                          \
  {                                                                     \
    size_t i;                                                           \
    for (i = 0; i < v->len; ++i)                                        \
      f (v->ptr[i]);                                                    \
  }                                                                     \
                                                                        \
  /* Sort the elements of the vector. */                                \
  static inline void __attribute__ ((__unused__))                       \
  name##_sort (name *v,                                                 \
               int (*compare) (const type *p1, const type *p2))         \
  {                                                                     \
    qsort (v->ptr, v->len, sizeof (type), (void *) compare);            \
  }                                                                     \
                                                                        \
  /* Remove duplicate adjacent elements (uniq). */                      \
  static inline void __attribute__ ((__unused__))                       \
  name##_uniq (name *v,                                                 \
               int (*compare) (type const *p1, type const *p2))         \
  {                                                                     \
    size_t i;                                                           \
    for (i = 0; i < v->len - 1; ++i) {                                  \
      if (compare (&v->ptr[i], &v->ptr[i+1]) == 0) {                    \
        name##_remove (v, i);                                           \
        i--;                                                            \
      }                                                                 \
    }                                                                   \
  }                                                                     \
                                                                        \
  /* Search for an exactly matching element in the vector using a       \
   * binary search.  Returns a pointer to the element or NULL.          \
   */                                                                   \
  static inline type * __attribute__ ((__unused__))                     \
  name##_search (const name *v, const void *key,                        \
                 int (*compare) (const void *key, const type *v))       \
  {                                                                     \
    return bsearch (key, v->ptr, v->len, sizeof (type),                 \
                    (void *) compare);                                  \
  }                                                                     \
                                                                        \
  /* Make a new vector with the same elements. */                       \
  static inline int __attribute__ ((__unused__))                        \
  name##_duplicate (name *v, name *copy)                                \
  {                                                                     \
    /* Note it's allowed for v and copy to be the same pointer. */      \
    type const *vptr = v->ptr;                                          \
    type *newptr;                                                       \
    const size_t len = v->len * sizeof (type);                          \
                                                                        \
    newptr = malloc (len);                                              \
    if (newptr == NULL) return -1;                                      \
    memcpy (newptr, vptr, len);                                         \
    copy->ptr = newptr;                                                 \
    copy->len = copy->cap = v->len;                                     \
    return 0;                                                           \
  }                                                                     \
                                                                        \
  /* End with duplicate declaration, so callers must supply ';'. */     \
  struct name

#define empty_vector { .ptr = NULL, .len = 0, .cap = 0 }

/* This macro should only be used if:
 * - the vector contains pointers, and
 * - the pointed-to objects are:
 *   - neither const- nor volatile-qualified, and
 *   - allocated with malloc() or equivalent.
 */
#define ADD_VECTOR_EMPTY_METHOD(name)                                  \
  /* Call free() on each element of the vector, then reset the vector. \
   */                                                                  \
  static inline void __attribute__ ((__unused__))                      \
  name##_empty (name *v)                                               \
  {                                                                    \
    size_t i;                                                          \
    for (i = 0; i < v->len; ++i) {                                     \
      STATIC_ASSERT (TYPE_IS_POINTER (v->ptr[i]),                      \
                     _vector_contains_pointers);                       \
      free (v->ptr[i]);                                                \
    }                                                                  \
    name##_reset (v);                                                  \
  }                                                                    \
                                                                       \
  /* Force callers to supply ';'. */                                   \
  struct name

/* Convenience macro tying together DEFINE_VECTOR_TYPE() and
 * ADD_VECTOR_EMPTY_METHOD(). Inherit and forward the requirement for a
 * trailing semicolon from ADD_VECTOR_EMPTY_METHOD() to the caller.
 */
#define DEFINE_POINTER_VECTOR_TYPE(name, type) \
  DEFINE_VECTOR_TYPE (name, type);             \
  ADD_VECTOR_EMPTY_METHOD (name)

struct generic_vector {
  void *ptr;
  size_t len;
  size_t cap;
};

extern int generic_vector_reserve (struct generic_vector *v,
                                   size_t n, size_t itemsize,
                                   bool exactly);

extern int generic_vector_reserve_page_aligned (struct generic_vector *v,
                                                size_t n, size_t itemsize);

#endif /* NBDKIT_VECTOR_H */
