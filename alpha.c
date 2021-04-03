#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "alpha.h"

/* TODO checking siblings during matching requires sibling list to be 
 * unordered; at each level, XOR leaf hashes and hash in number of children */

static int alpha_sibpush(struct alpha_siblist *siblist, struct alpha_node *ap) {
  siblist->sibs[siblist->num_sibs] = ap; 

  if (siblist->num_sibs + 1 >= siblist->len) {
    size_t newlen = siblist->len * 2;
    struct alpha_node **newmem = realloc(siblist->sibs, newlen);
    if (!newmem) {
      return ALPHA_RET_NOMEM;
    }
    siblist->sibs = newmem;
  }

  siblist->num_sibs += 1;
  return ALPHA_RET_OK;
}

static int alpha_sibpop(struct alpha_siblist *siblist, struct alpha_node *ap) {
  size_t i;
  for (i = 0; i < siblist->num_sibs; ++i) {
    if (ap == siblist->sibs[i]) {
      break;
    }
  }

  if (i == siblist->num_sibs) {
    return ALPHA_RET_NOTFOUND;
  }

  struct alpha_node *swapp = siblist->sibs[siblist->num_sibs];

  if (siblist->num_sibs - 1 <= (siblist->len / 2)) {
    size_t newlen = siblist->len / 2;
    struct alpha_node **newmem = realloc(siblist->sibs, newlen);
    if (!newmem) {
      return ALPHA_RET_NOMEM;
    }
    siblist->sibs = newmem;
  }

  siblist->sibs[i] = swapp;
  siblist->num_sibs -= 1;
  return ALPHA_RET_OK;
}

static void alpha_upddepth(struct alpha_node *ap, int depth); /* goes down tree */
static void alpha_rehash(struct alpha_node *ap); /* goes up tree */

/* TODO ensure empty AND nodes are created as children of CUT nodes */
static struct alpha_node *alpha_makenode_norehash(struct alpha_node *parent,
  const char *name, int type) {

  /* bare propositions should not have children */
  if (parent) {
    if (parent->type == ALPHA_TYPE_PROP) {
      return NULL;
    }

    if (parent->type == ALPHA_TYPE_CUT && parent->children.num_sibs > 0) {
      return NULL;
    }
  }
  
  char *namebuf = NULL;
  struct alpha_node *ap = NULL;
  struct alpha_node **sibs = NULL;

  /* propositions must have names */
  if (type == ALPHA_TYPE_PROP) {
    if (!name) {
      return NULL;
    }

    size_t namelen = strnlen(name, ALPHA_STR_MAXLEN);
    if (namelen >= ALPHA_STR_MAXLEN) {
      return NULL;
    } else {
      namebuf = malloc(namelen * sizeof(char));
      if (!namebuf) {
        return NULL;
      }
    }
    
    strncpy(namebuf, name, namelen);
  } 

  if (type == ALPHA_TYPE_AND) {
    sibs = malloc(ALPHA_VEC_SIZE * sizeof(struct alpha_node *));
    if (!sibs) {
      goto makenode_exc;
    }
  }

  ap = malloc(sizeof(struct alpha_node));
  if (!ap) {
    goto makenode_exc;
  }

  if (!parent) {
    ap->depth = 0;
  } else {
    ap->depth = parent->depth + 1;
    if (alpha_sibpush(&(parent->children), ap) != ALPHA_RET_OK) {
      goto makenode_exc;
    }
  }
  
  ap->children.sibs = sibs;
  ap->parent = parent;
  ap->name = namebuf;
  ap->type = type;
  alpha_rehash(ap);
  return ap;

makenode_exc: free(namebuf);
              free(sibs);
              free(ap);
              return NULL;
}

struct alpha_node *alpha_makenode(struct alpha_node *parent,
  const char *name, int type) {
  struct alpha_node *ap = alpha_makenode_norehash(parent, name, type);
  if (!ap) {
    return NULL;
  } else {
    alpha_rehash(ap);
    return ap;
  }
}

static void alpha_delnode_nopop(struct alpha_node *ap) {
  if (!ap) return;

  for (size_t i = 0; i < ap->children.num_sibs; ++i) {
    alpha_delnode_nopop(ap->children.sibs[i]);
  }

  free(ap->children.sibs);
  free(ap->name);
  free(ap);
}

void alpha_delnode(struct alpha_node *ap) {
  if (!ap) return;

  if (ap->parent) {
    alpha_sibpop(&(ap->children), ap);
  }

  alpha_delnode_nopop(ap);
}

/* Check if pasting the tree rooted at to_paste as a child of target would be a
 * valid exercise of the iteration rule. */
int alpha_chkpaste(struct alpha_node *target, struct alpha_node *to_paste) {
  if (!to_paste) {
    return ALPHA_RET_INVALID;
  }

  if (target == to_paste) {
    return ALPHA_RET_INVALID;
  } 

  if (target == to_paste->parent) {
    return ALPHA_RET_OK;
  } 

  if (!target->parent) {
    return ALPHA_RET_INVALID;
  } 

  return alpha_chkpaste(target->parent, to_paste);
}

/* TODO remove and add double negatives, returning the pre-existing child 
 * whose parentage has been affected by the operation */
struct alpha_node *alpha_remdneg(struct alpha_node *ap) {
}

struct alpha_node *alpha_adddneg(struct alpha_node *ap) {
}

/* Recompute the hash of ap and all its ancestors. The hash of a PROP
 * node is the djb2 hash of the string; the hash of an AND node is the XOR of
 * its childrens', multiplied by the number of children, and the hash of a NOT
 * node is the binary inverse of its childrens' hash + 1. */
static void alpha_rehash(struct alpha_node *ap) {
  if (!ap) {
    return;
  }

  hash_t newhash = 0;
  
  for (size_t i = 0; i < ap->children.num_sibs; ++i) {
    newhash ^= ap->children.sibs[i]->hash;
  }

switch (ap->type) {
  case ALPHA_TYPE_CUT :
    ap->hash = ~newhash + 1;
    break;
  case ALPHA_TYPE_AND :
    ap->hash = newhash * ap->children.num_sibs;
    break;
  case ALPHA_TYPE_PROP:
    /* djb2 hash: http://www.cse.yorku.ca/~oz/hash.html */
    newhash = 5381;
    for (size_t i = 0; ap->name[i]; ++i) {
      newhash = (newhash << 5) + newhash + ap->name[i];
    }
    ap->hash = newhash;
    break;
}

  alpha_rehash(ap->parent);
}

/* Update a node's depth after its parent's depth has been updated */
static void alpha_upddepth(struct alpha_node *ap, int depth) {
  if (!ap) return;
  ap->depth = 1;
  for (size_t i = 0; i < ap->children.num_sibs; ++i) {
    alpha_upddepth(ap->children.sibs[i], depth + 1);
  }
}

/* return 1 if two nodes are roots of structurally identical trees, 0 otherwise */
static int alpha_matchnode(struct alpha_node *a1p, struct alpha_node *a2p) {
  if ((a1p && !a2p) || (!a1p && a2p)) {
    return 0;
  } else if (!a1p && !a2p) {
    return 1;
  }

  if (a1p->type != a2p->type) {
    return 0;
  }
  
  switch (a1p->type) {
  case ALPHA_TYPE_CUT :
    break;
  case ALPHA_TYPE_AND :
    break;
  case ALPHA_TYPE_PROP:
    if (strncmp(a1p->name, a2p->name, ALPHA_STR_MAXLEN) != 0) {
      return 0;
    }
    break;
  default:
    return 0;
  }
}

