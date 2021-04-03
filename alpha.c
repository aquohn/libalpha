#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "alpha.h"

/* TODO checking siblings during matching requires sibling list to be 
 * unordered; at each level, XOR leaf hashes and hash in number of children */

static void alpha_delnode(struct alpha_node *ap);
static void alpha_updnode(struct alpha_node *ap, int depth);

struct alpha_node *alpha_makenode(struct alpha_node *parent,
  struct alpha_node *child, const char *name, int type) {

  if ((name && child) || (!name && !child)) {
    return NULL;
  }

  char *namebuf = NULL;
  size_t namelen = strnlen(name, ALPHA_STR_MAXLEN);
  if (namelen >= ALPHA_STR_MAXLEN) {
    return NULL;
  } else {
    namebuf = malloc(namelen * sizeof(char));
    if (!namebuf) {
      return NULL;
    }
  }

  struct alpha_node *ap = malloc(sizeof(struct alpha_node));
  if (!ap) {
    free(namebuf);
    return NULL;
  }

  if (!parent) {
    ap->depth = 0;
  } else {
    ap->depth = parent->depth + 1;
  }
  ap->parent = parent;

  if (child) {
    child->parent = ap;
    alpha_updnode(child, ap->depth + 1);
  }
  ap->child = child;
  
  strncpy(namebuf, name, namelen); /* TOCTOU */
  ap->name = namebuf;

  ap->type = type;

  return ap;
}

/* Recompute the hash of ap and all its ancestors. The hash of a PROP
 * node is the djb2 hash of the string; the hash of an AND node is the XOR of
 * its childrens', multiplied by the number of children, and the hash of a NOT
 * node is the binary inverse of its childrens' hash + 1. */
static void alpha_rehash(struct alpha_node *ap) {
  hash_t newhash = 0;
  size_t num_child = 0;
  for (struct alpha_node *child = ap->child; child != NULL; num_child++) {
    newhash ^= child->hash;
    child = child->nsib;
  }
}

/* Delete nodes within a tree being deleted */
static void alpha_delnode(struct alpha_node *ap) {
  if (!ap) return;

  alpha_delnode(ap->child);
  alpha_delnode(ap->nsib);
  
  free(ap->name);
  free(ap);
}

/* Delete tree rooted at this node */
void alpha_deltree(struct alpha_node *ap) {
  if (!ap) return;

  alpha_delnode(ap->child);
  if (ap->psib) {
    (ap->psib)->nsib = ap->nsib;
  }
  
  free(ap->name);
  free(ap);
}

/* Update a node's depth after its parent's depth has been updated */
static void alpha_updnode(struct alpha_node *ap, int depth) {
  if (!ap) return;

  alpha_updnode(ap->child, depth+1);
  alpha_updnode(ap->nsib, depth);
}

int alpha_match(struct alpha_node *a1p, struct alpha_node *a2p) {
  if ((a1p && !a2p) || (!a1p && a2p)) {
    return 0;
  } else if (!a1p && !a2p) {
    return 1;
  }

  if ((a1p->name && !a2p->name) || (!a1p->name && a2p->name)) {
    return 0;
  } else if (a1p->name && a2p->name) {
    if (strncmp(a1p->name, a2p->name, ALPHA_STR_MAXLEN) != 0) {
      return 0;
    }
  }

  /* TODO match tree vs match node */
  return alpha_match(a1p->child, a2p->child)
    && alpha_match(a1p->nsib, a2p->nsib);
}

/* Check if pasting the tree rooted at to_paste as a child of curr would be a
 * valid exercise of the iteration rule. */
int alpha_chkpaste(struct alpha_node *curr, struct alpha_node *to_paste) {
  if (!to_paste) {
    return 0;
  }

  if (curr == to_paste) {
    return 0;
  } 

  if (curr == to_paste->parent) {
    return 1;
  } 

  if (!curr->parent) {
    return 0;
  } 
 
  return alpha_chkpaste(curr->parent, to_paste);
}

/* TODO delete and paste, each with check */
int alpha_remdneg(struct alpha_node *ap) {
}

int alpha_adddneg(struct alpha_node *ap) {
}

