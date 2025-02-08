# `libalpha`: a graphical theorem prover backend

This project is a C library implementing logical core of Charles Sanders Peirce's system for the
graphical representation and manipulation of propositional logic, which
relies on the functional completeness of the connectives
$\{\land, \lnot\}$. Propositions are represented by textual symbols like
$p$ or $q$, while negations are represented by *cuts*, which may contain
other cuts and propositions, without intersecting them. The terms
(propositions and cuts) grouped together (or *juxtaposed*) within a
single cut represent a conjunction of those terms.

The advantages of a C implementation include
portability between different operating systems and environments,
flexibility in selecting the graphical frontend, separation of concerns
leading to a smaller, more maintainable logic core that is easier to
verify and optimise, and extensive tooling support. The library is
entirely self-contained, including simple hash function and dynamic
array implementations.

## Background

The natural data structure for this system is a tree, with propositions
as leaves and cuts as nodes, and a unique conjunction as the root node.
This easily fulfills the given definition of the Alpha existential
graphs. As a succinct reminder, writing the set of propositions/strings
as $P$, a cut around a diagram $D$ as $[D]$, and two juxtaposed diagrams
$D_1D_2$, the set of Alpha existential graphs is the closure of the set of
propositions under the operations of double cut ($D \mapsto [[D]]$) and juxtaposition.

Additionally, we define the *depth* of a node as the number of cuts that
enclose it. Therefore, the root-level conjunction and the cuts within it
have a depth of 0, while the children of the cuts (the terms within
them) have a depth of 1, and so on. This does not map as nicely to the
tree model, since the root conjunction is a special case, but for the
most part can be thought of as the depth of the nodes in the tree.
Marking the root conjunction as being at "depth $-1$" would have been
slightly more convenient in this respect, but would not really be
necessary and may lead to further complications.

The rules of inference are modelled as follows:
-   Erasure: nodes at an even depth can be erased.
-   Insertion: an arbitrary subgraph can be added as a child in a cut of
    odd depth.
-   Iteration: any subgraph $D$ can be added as a child of a node that
    is a descendant of $D$'s parent.
-   Deiteration: any subgraph $D$ that has a structurally identical copy
    $D'$ as a child of any of its ancestors may be deleted.
-   Double Cut: any node $v$ may be separated from its parent $u$, with
    two otherwise empty nodes $v_1$ and $v_2$ being inserted between
    them, such that the ancestries
    $u \to v$ and $u \to v_1 \to v_2 \to v$ are equivalent.
    Alternatively, we may write $D \Leftrightarrow [[D]].$

## Model

The two primary data structures are shown below, the
`struct alpha_siblist` dynamic array, and the `struct alpha_node` tree
node. The former is largely uninteresting, while the latter can be of
three possible types: `ALPHA_TYPE_AND`, the root conjunction,
`ALPHA_TYPE_CUT`, a cut, and `ALPHA_TYPE_PROP`, and proposition carrying
a string which is its name.

``` {.c firstline="33" lastline="46"}
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
```

One operation that could potentially be very costly is deiteration, as
in the worst case the whole tree might need to be manually verified in
order to find a matching subgraph among a given graph's ancestors. We
expedite this process with an implementation of the simple [`djb2`
string hash](http://www.cse.yorku.ca/~oz/hash.html) to generate the
hashes for individual propositions. The hash of a cut is the XOR of its
children's hashes, multiplied by the number of children the node has, in
order to encode the structure of the tree. The hashing algorithm is
shown below.

``` {.c firstline="348" lastline="367"}
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
```

Maintaining the hashes makes comparing the trees much faster on average,
at the cost of some update operations that must be performed whenever
operating on the tree.

The interface presented by the library is summarised by the following
header file excerpt:

``` {.c firstline="52" lastline="62"}
alpha_ret_t alpha_move(struct alpha_node *target, struct alpha_node *content);
alpha_ret_t alpha_reparent(struct alpha_node *target, struct alpha_node *content);
alpha_ret_t alpha_paste(struct alpha_node *target, struct alpha_node *content);

alpha_ret_t alpha_prfinsert(struct alpha_node *target, struct alpha_node *content);
alpha_ret_t alpha_prferase(struct alpha_node *target);
alpha_ret_t alpha_chkiter(struct alpha_node *target, struct alpha_node *content);
alpha_ret_t alpha_chkdeiter(struct alpha_node *ap);

alpha_ret_t alpha_remdneg(struct alpha_node *ap);
alpha_ret_t alpha_adddneg(struct alpha_node *ap);
```

The `makenode` and `delnode` functions are primitives for adding and
removing nodes from the data model while maintaining its internal
consistency. `move` (attach a node and its children to a different
parent), `paste` (create a copy of a subtree) and `reparent` (move a
node's children to another node, and delete the node) are slightly
higher-level functions that expose a powerful API to users for
efficiently but consistently manipulating the object model. They are
optimised by deferring the update and rehash operations until after the
whole transformation is complete. Finally, the `prfinsert`, `prferase`,
`chkiter`, `chkdeiter`, `remdneg` and `adddneg` functions implement the
valid manipulations for Alpha diagrams.

