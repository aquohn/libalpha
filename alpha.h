#include <stdint.h>

#ifndef ALPHA_H
#define ALPHA_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ALPHA_STR_MAXLEN
#define ALPHA_STR_MAXLEN 50
#endif
#ifndef ALPHA_VEC_SIZE
#define ALPHA_VEC_SIZE 64
#endif

#define ALPHA_TYPE_CUT 0
#define ALPHA_TYPE_AND 1
#define ALPHA_TYPE_PROP 2

typedef uint64_t hash_t;
struct alpha_node;
struct alpha_siblist;

struct alpha_siblist {
  size_t num_sibs;
  struct alpha_node *sibs;
};

struct alpha_node {
  struct alpha_node *parent;
  struct alpha_node *child;
  struct alpha_siblist sibs;
  char *name;
  int type;
  size_t depth;
  hash_t hash;
};

struct alpha_node *alpha_makenode(struct alpha_node *parent,
  struct alpha_node *child, const char *name, int type);

void alpha_deltree(struct alpha_node *ap);
int alpha_match(struct alpha_node *a1p, struct alpha_node *a2p);

#ifdef __cplusplus
}
#endif

#endif /* ALPHA_H */
