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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>

#include "avltree.h"

static void
init_node (struct avltree *node)
{
  node->left = node->right = NULL;
  node->height = 1;
}

/* Return the height of a node, or 0 if NULL. */
static int
get_height (struct avltree *node)
{
  if (node != NULL)
    return node->height;
  else
    return 0;
}

/* Update the height of a node. */
static void
update_height (struct avltree *node)
{
  int left_height = get_height (node->left);
  int right_height = get_height (node->right);

  if (left_height > right_height)
    node->height = left_height + 1;
  else
    node->height = right_height + 1;
}

/* Returns the balance factor of a node, used to determine how we
 * will rebalance it.
 */
static int
get_balance (struct avltree *node)
{
  if (node != NULL)
    return get_height (node->left) - get_height (node->right);
  else
    return 0;
}

/* Do the "left rotate" rebalancing operation. */
static struct avltree *
left_rotate (struct avltree *x)
{
  struct avltree *y = x->right;
  struct avltree *t2 = y->left;

  y->left = x;
  x->right = t2;
  update_height (x);
  update_height (y);
  return y;
}

/* Do the "right rotate" rebalancing operation. */
static struct avltree *
right_rotate (struct avltree *y)
{
  struct avltree *x = y->left;
  struct avltree *t2 = x->right;

  x->right = y;
  y->left = t2;
  update_height (y);
  update_height (x);
  return x;
}

void *
avltree_insert (void *tv, void *nv, avltree_comparison_fn compare)
{
  struct avltree *tree = tv;
  struct avltree *node = nv;
  int r;
  int b;

  /* Initialize the struct avltree in the new node. */
  init_node (node);

  /* Inserting a node into an empty tree. */
  if (tree == NULL)
    return node;

  r = compare (node, tree);
  if (r < 0)                    /* New node goes into left branch. */
    tree->left = avltree_insert (tree->left, node, compare);
  else if (r > 0)               /* New node goes into right branch. */
    tree->right = avltree_insert (tree->right, node, compare);
  else
    /* All nodes must have distinct values.  Return the unmodified tree. */
    return tree;

  /* Update height field of root node. */
  update_height (tree);

  /* Rebalance the tree. */
  b = get_balance (tree);

  /* Left-left. */
  if (b > 1 && compare (node, tree->left) < 0)
    return right_rotate (tree);
  /* Right-right. */
  else if (b < -1 && compare (node, tree->right) > 0)
    return left_rotate (tree);
  /* Left-right. */
  else if (b > 1 && compare (node, tree->left) > 0) {
    tree->left = left_rotate (tree->left);
    return right_rotate (tree);
  }
  /* Right-left. */
  else if (b < -1 && compare (node, tree->right) < 0) {
    tree->right = right_rotate (tree->right);
    return left_rotate (tree);
  }
  /* Tree still balanced. */
  else
    return tree;
}
