#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "alpha.h"

static alpha_ret_t alpha_sibpush(struct alpha_siblist *siblist, struct alpha_node *ap);
static alpha_ret_t alpha_sibpop(struct alpha_siblist *siblist, struct alpha_node *ap);

static struct alpha_node *alpha_makenode_norehash(struct alpha_node *parent,
    const char *name, int type, alpha_ret_t *retp);
static void alpha_delnode_nopop(struct alpha_node *ap);
static void alpha_upddepth(struct alpha_node *ap, size_t depth); /* goes down tree */
static void alpha_rehash(struct alpha_node *ap); /* goes up tree */

static alpha_ret_t alpha_recurmatch(struct alpha_node *target, struct alpha_node *curr);
static alpha_ret_t alpha_matchnode(struct alpha_node *a1p, struct alpha_node *a2p);
static alpha_ret_t alpha_matchconj(struct alpha_node *a1p, struct alpha_node *a2p);
static alpha_ret_t alpha_paste_norehash(struct alpha_node *target, struct alpha_node *content);

struct alpha_node *alpha_makenode(struct alpha_node *parent,
    const char *name, alpha_type_t type, alpha_ret_t *retp) {
  struct alpha_node *ap = alpha_makenode_norehash(parent, name, type, retp);
  if (!ap) {
    return NULL;
  } else {
    alpha_rehash(ap);
    return ap;
  }
}

void alpha_delnode(struct alpha_node *ap) {
  if (!ap) return;

  if (ap->parent) {
    alpha_sibpop(&(ap->parent->children), ap);
  }

  alpha_delnode_nopop(ap);
}

/* Insert content at target if it would be a valid inference to do so */
alpha_ret_t alpha_prfinsert(struct alpha_node *target, struct alpha_node *content) {
  /* insertion only valid at odd depths */
  if (!target || !content || target->depth % 2 != 1) {
    return ALPHA_RET_INVALID;
  }
  
  return alpha_paste(target, content);
}

/* Delete target if it would be a valid inference to do so */
alpha_ret_t alpha_prferase(struct alpha_node *target) {
  /* Erasure only valid at even depths */
  if (target->depth % 2 != 0) {
    return ALPHA_RET_INVALID;
  }

  alpha_delnode(target);
  return ALPHA_RET_OK;
}

/* Check if pasting the tree rooted at content as a child of target would be a
 * valid exercise of the iteration rule. */
alpha_ret_t alpha_chkiter(struct alpha_node *target, struct alpha_node *content) {
  /* there must be a root node */
  if (!content) {
    return ALPHA_RET_INVALID;
  }

  /* cannot iter a diagram within itself */
  if (target == content) {
    return ALPHA_RET_INVALID;
  } 

  /* we must eventually find the parent of the tree being iterd*/
  if (target == content->parent) {
    return ALPHA_RET_OK;
  }

  /* NULL has no parents; either we succeed before reaching here or we fail */
  if (!target) {
    return ALPHA_RET_INVALID;
  }

  /* recurse and check the parent of this target */
  return alpha_chkiter(target->parent, content);
}

/* TODO additional fn: find all matches and return them in a siblist */

/* Check if the tree rooted at this node can be removed via deiteration */
alpha_ret_t alpha_chkdeiter(struct alpha_node *ap) {
  if (!ap || !(ap->parent)) {
    return ALPHA_RET_INVALID;
  }
  return alpha_recurmatch(ap->parent, ap);
}

/* construct a tree structurally identical to the one rooted at content,
 * rooting content's clone at target */
alpha_ret_t alpha_paste(struct alpha_node *target, 
    struct alpha_node *content) {
  alpha_ret_t ret = alpha_paste_norehash(target, content);
  if (ret != ALPHA_RET_OK) {
    return ret;
  }
  /* depth update will be handled by makenode */
  alpha_rehash(target);
  return ret;
}

/* move a subtree rooted at content, rooting content at target */
alpha_ret_t alpha_move(struct alpha_node *target, 
    struct alpha_node *content) {

  alpha_ret_t ret;
  if (!content || !target) {
    /* subtree root must exist, and cannot change root conjunction */
    ret = ALPHA_RET_INVALID;
    goto move_exc;
  }
  
  if (target->type == ALPHA_TYPE_PROP) {
    ret = ALPHA_RET_INVALID;
    goto move_exc;
  }

  /* TODO handle errors */
  struct alpha_node *oldparent = content->parent;
  alpha_sibpush(&(target->children), content);
  if (oldparent) {
    alpha_sibpop(&(oldparent->children), content);
  }
  content->parent = target;
  
  alpha_upddepth(content, target->depth);
  alpha_rehash(target);
  ret = ALPHA_RET_OK;

move_exc: return ret;
}

/* move all children of content to target, and delete content */
alpha_ret_t alpha_reparent(struct alpha_node *target, 
    struct alpha_node *content) {

  alpha_ret_t ret;
  if (!content || !target) {
    /* subtree root must exist, and cannot change root conjunction */
    ret = ALPHA_RET_INVALID;
    goto reparent_exc;
  }
  
  if (target->type == ALPHA_TYPE_PROP) {
    ret = ALPHA_RET_INVALID;
    goto reparent_exc;
  }

  /* TODO: create a temporary object and push into it first; right now this is a
   * fatal error */
  for (size_t i = 0; i < content->children.num_sibs; ++i) {
    struct alpha_node *child = content->children.sibs[i];
    if (alpha_sibpush(&(target->children), child) != ALPHA_RET_OK) {
      ret = ALPHA_RET_FATAL;
      goto reparent_exc;
    } else {
      child->parent = target;
      alpha_upddepth(child, target->depth);
    }
  }
  content->children.num_sibs = 0; /* tricks cleanup code into not deleting the children */
  alpha_delnode(content);
  
  alpha_rehash(target);
  ret = ALPHA_RET_OK;

reparent_exc: return ret;
}

/* remove a double cut, where ap is the outermost cut */
alpha_ret_t alpha_remdneg(struct alpha_node *ap) {
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

  return alpha_reparent(parent, child);
}

/* add a double negative around ap, returning the parent of the outermost cut */
alpha_ret_t alpha_adddneg(struct alpha_node *ap) {
  if (!ap) {
    return ALPHA_RET_INVALID; /* NULL cannot be a child */
  }

  struct alpha_node *parent = ap->parent;
  if (!parent) {
    return ALPHA_RET_INVALID; /* cannot negate root element */
  }

  /* TODO error handling */
  alpha_ret_t ret;
  struct alpha_node *outcut = alpha_makenode_norehash(parent, NULL, ALPHA_TYPE_CUT, &ret);
  struct alpha_node *incut = alpha_makenode_norehash(outcut, NULL, ALPHA_TYPE_CUT, &ret);
  return alpha_move(incut, ap);
}

/* Static functions */

static struct alpha_node *alpha_makenode_norehash(struct alpha_node *parent,
    const char *name, int type, alpha_ret_t *retp) {

  char *namebuf = NULL;
  struct alpha_node *ap = NULL;
  struct alpha_node **sibs = NULL;

  if (parent) {
    /* bare propositions should not have children */
    if (parent->type == ALPHA_TYPE_PROP) {
      *retp = ALPHA_RET_INVALID;
      goto makenode_exc;
    }

    /* only the top level conjunction is of type AND */
    if (type == ALPHA_TYPE_AND) {
      *retp = ALPHA_RET_INVALID;
      goto makenode_exc;
    }
  }

  /* propositions must have names */
  if (type == ALPHA_TYPE_PROP) {
    if (!name) {
      *retp = ALPHA_RET_INVALID;
      goto makenode_exc;
    }

    size_t namelen = strnlen(name, ALPHA_STR_MAXLEN);
    if (namelen >= ALPHA_STR_MAXLEN) {
      *retp = ALPHA_RET_INVALID;
      goto makenode_exc;
    } else {
      namebuf = malloc(namelen * sizeof(char));
      if (!namebuf) {
        *retp = ALPHA_RET_INVALID;
        goto makenode_exc;
      }
    }

    strncpy(namebuf, name, namelen);
  } else { /* propositions cannot have children */
    sibs = malloc(ALPHA_VEC_SIZE * sizeof(struct alpha_node *));
    if (!sibs) {
      *retp = ALPHA_RET_NOMEM;
      goto makenode_exc;
    }
  }

  ap = malloc(sizeof(struct alpha_node));
  if (!ap) {
    *retp = ALPHA_RET_NOMEM;
    goto makenode_exc;
  }

  if (!parent) {
    ap->depth = 0;
  } else {
    alpha_upddepth(ap, parent->depth);
    if (alpha_sibpush(&(parent->children), ap) != ALPHA_RET_OK) {
      *retp = ALPHA_RET_NOMEM;
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
  *retp = ALPHA_RET_OK;
  return ap;

makenode_exc: free(namebuf);
              free(sibs);
              free(ap);
              return NULL;
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

/* Update a node's depth, given its parent's depth */
static void alpha_upddepth(struct alpha_node *ap, size_t depth) {
  if (!ap) return;
  if (ap->type == ALPHA_TYPE_CUT) {
    ap->depth = depth + 1;
    for (size_t i = 0; i < ap->children.num_sibs; ++i) {
      alpha_upddepth(ap->children.sibs[i], ap->depth);
    }
  } else {
    ap->depth = depth;
  }
}

/* Create a copy of the tree rooted at content as a child of target;
 * validity must be checked by the caller */
static alpha_ret_t alpha_paste_norehash(struct alpha_node *target, 
    struct alpha_node *content) {
  alpha_ret_t ret;
  if (!content) {
    return ALPHA_RET_INVALID;
  }

  /* TODO have an outer function delete everything created on failure */
  struct alpha_node *newnode = NULL;
  if (content->type == ALPHA_TYPE_AND) { 
    /* paste children, not the AND node itself */
    newnode = target;
  } else {
    alpha_makenode_norehash(target, 
        content->name, content->type, &ret);
    if (ret != ALPHA_RET_OK) {
      ret = ALPHA_RET_FATAL;
      goto paste_exc;
    }
    ret = alpha_sibpush(&(target->children), newnode);
    if (ret != ALPHA_RET_OK) {
      ret = ALPHA_RET_FATAL;
      goto paste_exc;
    }
  }

  for (size_t i = 0; i < content->children.num_sibs; ++i) {
    ret = alpha_paste_norehash(newnode, content->children.sibs[i]);
    if (ret != ALPHA_RET_OK) {
      ret = ALPHA_RET_FATAL;
      goto paste_exc;
    }
  }
paste_exc: return ret;
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


/* Recursively check if there is a node structurally identical to target 
 * rooted at curr or any descendant thereof */
static alpha_ret_t alpha_recurmatch(struct alpha_node *target, struct alpha_node *curr) {
  if (alpha_matchnode(curr, target) == ALPHA_RET_OK) {
    return ALPHA_RET_OK;
  } else if (!curr || !target) {
    return ALPHA_RET_INVALID;
  }

  for (size_t i = 0; i < curr->children.num_sibs; ++i) {
    if (alpha_recurmatch(curr->children.sibs[i], curr) == ALPHA_RET_OK) {
      return ALPHA_RET_OK;
    } 
  }

  return ALPHA_RET_INVALID;
}

/* Check if the trees rooted at two nodes are structurally identical */
static alpha_ret_t alpha_matchnode(struct alpha_node *a1p, struct alpha_node *a2p) {
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

static alpha_ret_t alpha_matchconj(struct alpha_node *a1p, struct alpha_node *a2p) {
  /* no time to implement hash based indexing; just use hash to speed up checks */
  alpha_ret_t ret = ALPHA_RET_OK;
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

static alpha_ret_t alpha_sibpush(struct alpha_siblist *siblist, struct alpha_node *ap) {
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

static alpha_ret_t alpha_sibpop(struct alpha_siblist *siblist, struct alpha_node *ap) {
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

