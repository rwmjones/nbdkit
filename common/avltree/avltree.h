/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
 * All rights reserved.
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

/* Simple implementation of a self-balancing AVL tree. */

#ifndef AVLTREE_H
#define AVLTREE_H

/* A node in the tree.
 *
 * To use the tree, extend this struct with any extra per-node
 * data that you want to store, eg:
 *
 * struct mynode {
 *   struct avltree tree; // must be first in the struct
 *   // any extra per-node data you want to store goes here
 * };
 *
 * and then pass the pointer to struct mynode (as 'void *') to
 * the insert function.
 *
 * You must supply a comparison function to compare two nodes
 * which will return if one node is <, = or > than the other.
 * All nodes in a tree must have distinct keys.  If you try to
 * insert a node into a tree where an existing node has the
 * same key already, then the existing tree is returned unmodified.
 *
 * Note there are no lookup functions supplied since you can
 * implement those by searching through the tree according to
 * the requirements of the application.
 *
 * There is no delete function, partly because we don't need it
 * for our use case, and partly because the structure makes it
 * difficult to implement (as you'd have to copy nodes around,
 * making all existing node pointers invalid).
 */
struct avltree {
  struct avltree *left, *right;
  int height;
};

/* User-supplied comparison function.  Must return:
 *   < 0  if node1 < node2
 *   = 0  if node1 = node2
 *   > 0  if node1 > node2
 */
typedef int (*avltree_comparison_fn) (const void *node1, const void *node2);

/* Insert a new node in the tree rooted at 'tree'.
 * tree may be NULL to insert the node into a new tree.
 * Returns the new root node.
 */
extern void *avltree_insert (void *tree, void *node,
                             avltree_comparison_fn compare);

#endif /* AVLTREE_H */
