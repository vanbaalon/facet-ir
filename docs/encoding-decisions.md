# Facet Gen 1 Encoding Decisions

> Status: implementation contract for the current Gen 1 prototype.
> This document records deliberate choices where the implementation differs from
> examples in the founding design notes.

## Scope

Facet Gen 1 is a representation and projection layer. It does not evaluate,
simplify, solve, or certify expressions. The contract below defines the tree
shape used by the C++ library, CLI, corpus tests, and future bridges.

## Core Binder Shape

The canonical Core binder shape is:

```lisp
(binder <name-or-meta> <domain>)
```

Examples:

```lisp
(int (binder x (range 0 1)) (sin (* pi x)))
(forall (binder x R) (>= (^ x 2) 0))
(exists (binder (meta F :kind one) Smooth) ...)
(lam (binder x _) (^ x 2))
```

This intentionally differs from the founding-design example:

```lisp
(binder (x : (range 0 1)))
```

The implementation keeps the binder node as a simple two-child product because
it is isomorphic to Strict and Object encodings, avoids a special tuple node,
and makes binder consumers direct: child 0 is the bound term, child 1 is the
domain or placeholder.

Surface syntax still prints binders as `op[x : D](body)` or, for limits,
`op[x -> a](body)`.

## Object Atom Encoding

Facet Object uses typed atom objects:

```json
{"atom":"sym","value":"x"}
{"atom":"int","value":"42"}
{"atom":"rat","value":"1/2"}
{"atom":"real","value":"1.25"}
{"atom":"str","value":"literal"}
```

Compounds use:

```json
{"head":"sin","args":[{"atom":"sym","value":"x"}]}
```

This intentionally differs from the founding-design shorthand where symbols and
strings appear as bare JSON strings and integers appear as JSON numbers. Typed
atoms remove ambiguity between a symbol named `x` and a string value `"x"`, and
preserve arbitrary-precision numeric text without JSON number limitations.

Unknown object keys are ignored so newer metadata such as spans or source
annotations can be skipped by older readers. Expression-bearing fields remain
limited to the known `head`, `args`, `attrs`, `atom`, and `value` contract.

## Attribute Rules

Attributes are stored on compound nodes as sorted key/value pairs:

```lisp
(simplify (sqrt (^ x 2)) :assume (>= x 0) :via sympy)
```

Attribute keys are unique per node. Attempting to construct duplicate keys is
an error. Sorting makes hashing, printing, and object output canonical.

The surface context operator lowers local contexts into attributes only when the
target is already a compound and the context has one argument:

```facet
simplify(x) @ assume(x > 0)
```

becomes:

```lisp
(simplify x :assume (> x 0))
```

Context on atoms remains explicit to avoid changing an atom into a compound:

```facet
x @ assume(y > 0)
```

becomes:

```lisp
(@ x (assume (> y 0)))
```

## Meta Variables

Surface meta variables lower to explicit `meta` compounds:

```facet
?x       -> (meta x :kind one)
?xs...   -> (meta xs :kind seq)
?xs...?  -> (meta xs :kind seq?)
```

The `kind` attribute is part of the canonical tree. Repeated meta names are
matched structurally by later pattern tooling; Gen 1 only represents them.

## Rule And Goal Wrappers

Surface rules infer universal metas and store them explicitly:

```facet
rule pyth: sin(?a)^2 + cos(?a)^2 ~> 1 when ?a > 0
```

becomes:

```lisp
(rule pyth
  (forall
    (meta a :kind one)
    (~> (+ (^ (sin (meta a :kind one)) 2)
           (^ (cos (meta a :kind one)) 2))
        1)
    :when (> (meta a :kind one) 0)))
```

Goals are labelled wrappers around their body:

```facet
goal g: exists[?F : Smooth](...)
```

becomes:

```lisp
(goal g (exists (binder (meta F :kind one) Smooth) ...))
```

## Numeric Storage

Integers, rationals, and reals are stored as text in Gen 1. This preserves
transport fidelity but does not yet normalize numbers. For example, rationals
are validated enough to reject zero denominators, but there is no GMP-backed
canonicalization yet.

## Stability Notes

These encodings are now covered by the corpus in `tests/corpus/gen1.txt`.
Changes to this contract should update this document, the corpus, and bridge
emitters/readers in the same commit.
