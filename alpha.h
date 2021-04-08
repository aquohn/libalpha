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
#define ALPHA_VEC_SIZE 16
#endif

#define ALPHA_TYPE_CUT 0
#define ALPHA_TYPE_AND 1
#define ALPHA_TYPE_PROP 2

#define ALPHA_RET_OK 1
#define ALPHA_RET_FATAL 0
#define ALPHA_RET_INVALID -1
#define ALPHA_RET_NOTFOUND -2
#define ALPHA_RET_NOMEM -3

typedef uint64_t hash_t;
typedef int alpha_ret_t;
typedef int alpha_type_t;
struct alpha_node;
struct alpha_siblist;

struct alpha_siblist {
  size_t num_sibs;
  size_t len;
  struct alpha_node **sibs;
};

struct alpha_node {
  struct alpha_node *parent;
  struct alpha_siblist children;
  char *name;
  alpha_type_t type;
  size_t depth;
  hash_t hash;
};

struct alpha_node *alpha_makenode(struct alpha_node *parent,
  const char *name, alpha_type_t type, alpha_ret_t *retp);
void alpha_delnode(struct alpha_node *ap);

alpha_ret_t alpha_prfinsert(struct alpha_node *target, struct alpha_node *content);
alpha_ret_t alpha_prferase(struct alpha_node *target);

alpha_ret_t alpha_paste(struct alpha_node *target, struct alpha_node *content);
alpha_ret_t alpha_chkiter(struct alpha_node *target, struct alpha_node *content);
alpha_ret_t alpha_chkdeiter(struct alpha_node *ap);

alpha_ret_t alpha_remdneg(struct alpha_node *ap);
struct alpha_node *alpha_adddneg(struct alpha_node *ap);

#ifdef __cplusplus
}
#endif

#endif /* ALPHA_H */
