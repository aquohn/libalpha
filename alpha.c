#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "alpha.h"

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
static int alpha_recurmatch(struct alpha_node *target, struct alpha_node *curr);
static int alpha_matchnode(struct alpha_node *a1p, struct alpha_node *a2p);
static int alpha_matchconj(struct alpha_node *a1p, struct alpha_node *a2p);
static int alpha_paste_norehash(struct alpha_node *target, struct alpha_node *to_paste);

/* TODO root is not NULL; root's parent is NULL but there should be only one
 * root */

static struct alpha_node *alpha_makenode_norehash(struct alpha_node *parent,
  const char *name, int type) {

  /* bare propositions should not have children */
  if (parent) {
    if (parent->type == ALPHA_TYPE_PROP) {
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
  } else {
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
   
  ap->children.num_sibs = 0;
  /* NOTE potential gotcha: sibs is NULL for ALPHA_TYPE_PROP. Functions here
   * are careful to avoid dereferencing it unless it cannot be ALPHA_TYPE_PROP,
   * but extensions/maintainers should be aware of this */
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
  /* not possible to paste the whole tree */
  if (!to_paste) {
    return ALPHA_RET_INVALID;
  }
  
  /* cannot paste a diagram within itself */
  if (target == to_paste) {
    return ALPHA_RET_INVALID;
  } 

  /* we must eventually find the parent of the tree being pasted*/
  if (target == to_paste->parent) {
    return ALPHA_RET_OK;
  }
  
  /* NULL has no parents; either we succeed before reaching here or we fail */
  if (!target) {
    return ALPHA_RET_INVALID;
  }
  
  /* recurse and check the parent of this target */
  return alpha_chkpaste(target->parent, to_paste);
}

/* Create a copy of the tree rooted at to_paste as a child of target */
static int alpha_paste_norehash(struct alpha_node *target, 
    struct alpha_node *to_paste) {
  if (!target || !to_paste) {
    return ALPHA_RET_INVALID;
  }

  if (target->type == ALPHA_TYPE_PROP) {
    return ALPHA_RET_INVALID;
  }
  
  /* TODO catch when malloc fails */
  struct alpha_node *newnode = alpha_makenode_norehash(target, 
      to_paste->name, to_paste->type);
  alpha_sibpush(&(target->children), newnode);
  for (size_t i = 0; i < to_paste->children.num_sibs; ++i) {
    alpha_paste_norehash(newnode, to_paste->children.sibs[i]);
  }

  return ALPHA_RET_OK;
}

int alpha_paste(struct alpha_node *target, 
    struct alpha_node *to_paste) {
  int ret = alpha_paste_norehash(target, to_paste);
  if (ret != ALPHA_RET_OK) {
    return ret;
  }
  alpha_rehash(target);
  return ret;
}

/* Check if the tree rooted at this node can be removed via deiteration */
int alpha_chkdeiter(struct alpha_node *ap) {
  if (!ap || !(ap->parent)) {
    return ALPHA_RET_INVALID;
  }
  return alpha_recurmatch(ap->parent, ap);
}

/* remove a double cut, where ap is the outermost cut */
int alpha_remdneg(struct alpha_node *ap) {
  if (!ap) {
    return ALPHA_RET_INVALID;
  }

  if (ap->type != ALPHA_TYPE_CUT) {
    return ALPHA_RET_INVALID;
  }

  struct alpha_node *parent = ap->parent;
  if (!parent) {
    return ALPHA_RET_INVALID;
  }

  if (ap->children.num_sibs != 1) {
    return ALPHA_RET_INVALID;
  }

  struct alpha_node *child = ap->children.sibs[0];

  if (!child) {
    return ALPHA_RET_INVALID;
  }

  if (child->type != ALPHA_TYPE_CUT) {
    return ALPHA_RET_INVALID;
  }
  
  /* TODO: create a temporary object and push into it first; right now this is a
   * fatal error */
  for (size_t i = 0; i < child->children.num_sibs; ++i) {
    if (alpha_sibpush(&(parent->children), child->children.sibs[i]) == ALPHA_RET_NOMEM) {
      return ALPHA_RET_FATAL;
    }
  }
  child->children.num_sibs = 0; /* tricks cleanup code into not deleting the children */
  alpha_delnode(ap);

  return ALPHA_RET_OK;
}

/* add a double negative around ap, returning the outermost cut */
struct alpha_node *alpha_adddneg(struct alpha_node *ap) {
  if (!ap || !ap->parent) {
    return NULL; /* invalid */
  }
  
  struct alpha_node *parent = ap->parent;
  /* TODO catch if malloc fails */
  struct alpha_node *outcut = alpha_makenode_norehash(parent, NULL, ALPHA_TYPE_CUT);
  struct alpha_node *incut = alpha_makenode_norehash(outcut, NULL, ALPHA_TYPE_CUT);
  for (size_t i = 0; i < parent->children.num_sibs; ++i) {
    if (parent->children.sibs[i] == ap) {
      parent->children.sibs[i] = outcut;
    }
  }

  ap->parent = incut;
  alpha_sibpush(&(incut->children), ap);
  alpha_rehash(incut);
  alpha_upddepth(ap, incut->depth + 1);
  return outcut;
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
  case ALPHA_TYPE_CUT:
    ap->hash = ~newhash + 1;
    break;
  case ALPHA_TYPE_AND:
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

/* Recursively check if there is a node structurally identical to target 
 * rooted at curr or any descendant thereof */
static int alpha_recurmatch(struct alpha_node *target, struct alpha_node *curr) {
  if (alpha_matchnode(curr, target) == ALPHA_RET_OK) {
    return ALPHA_RET_OK;
  }

  for (size_t i = 0; i < curr->children.num_sibs; ++i) {
    if (alpha_recurmatch(curr->children.sibs[i], curr) == ALPHA_RET_OK) {
      return ALPHA_RET_OK;
    } 
  }

  return ALPHA_RET_INVALID;
}

/* Check if the trees rooted at two nodes are structurally identical */
static int alpha_matchnode(struct alpha_node *a1p, struct alpha_node *a2p) {
  if ((a1p && !a2p) || (!a1p && a2p)) {
    return ALPHA_RET_INVALID;
  } else if (!a1p && !a2p) {
    return ALPHA_RET_OK;
  }

  if (a1p->type != a2p->type) {
    return ALPHA_RET_INVALID;
  }

  if (a1p->hash != a2p->hash) {
    return ALPHA_RET_INVALID;
  }
  
  switch (a1p->type) {
  case ALPHA_TYPE_CUT: /* fallthrough */
  case ALPHA_TYPE_AND: 
    return alpha_matchconj(a1p, a2p);
    break;
  case ALPHA_TYPE_PROP:
    if (strncmp(a1p->name, a2p->name, ALPHA_STR_MAXLEN) == 0) {
      return ALPHA_RET_OK;
    } else {
      return ALPHA_RET_INVALID;
    }
    break;
  default:
    return ALPHA_RET_INVALID;
  }
}

static int alpha_matchconj(struct alpha_node *a1p, struct alpha_node *a2p) {
    /* no time to implement hash based indexing; just use hash to speed up checks */
    int ret = ALPHA_RET_OK;
    size_t num_sibs1 = a1p->children.num_sibs;
    size_t num_sibs2 = a2p->children.num_sibs;

    if (num_sibs1 == 0 && num_sibs2 == 0) {
      return ALPHA_RET_OK;
    }

    if (num_sibs1 != num_sibs2) {
      return ALPHA_RET_INVALID;
    }
    
    /* create local copy of pointers to a2p's children */
    int copy_succeeded = 0;
    struct alpha_node **sibs2 = malloc(num_sibs2 * sizeof(struct alpha_node *));
    if (!sibs2) {
      sibs2 = a2p->children.sibs;
    } else {
      copy_succeeded = 1;
      memcpy(sibs2, a2p->children.sibs, num_sibs2 * sizeof(struct alpha_node *));
    }

    for (size_t i = 0; i < num_sibs1; ++i) {
      struct alpha_node *curr1 = a1p->children.sibs[i];
      struct alpha_node *curr2 = NULL;
      for (size_t j = 0; j < num_sibs2; ++j) {
        if (!sibs2[j]) {
          continue;
        }

        if (curr1->hash == sibs2[j]->hash && alpha_matchnode(curr1, sibs2[j])) {
          curr2 = sibs2[j];
          /* mark nodes already used as NULL */
          if (copy_succeeded) {
            sibs2[j] = NULL;
          }
          break;
        }
      }
      if (!curr2) {
        ret = ALPHA_RET_INVALID;
        break;
      }
    }

    if (copy_succeeded) {
      free(sibs2);
    }
    return ret;
}
