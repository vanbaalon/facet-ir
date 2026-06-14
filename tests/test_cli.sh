#!/bin/sh
set -eu

facet="${1:-build/facet}"

expect() {
  name="$1"
  input="$2"
  shift 2
  got="$(printf '%s' "$input" | "$facet" "$@")"
  if [ "$got" != "$EXPECT" ]; then
    printf 'FAIL: %s\n  got : %s\n  want: %s\n' "$name" "$got" "$EXPECT" >&2
    exit 1
  fi
}

EXPECT='Integral(sin(pi*x), (x, 0, 1))'
expect "source:sympy alias" 'int[x : 0..1](sin(pi*x))' emit=source:sympy

EXPECT='(int (binder x (range 0 1)) (sin (* pi x)))'
expect "source:sympy-srepr read alias" \
  "Integral(sin(Mul(pi, Symbol('x'))), Tuple(Symbol('x'), Integer(0), Integer(1)))" \
  read=source:sympy-srepr emit=core

EXPECT='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 400"><circle cx="200" cy="200" r="3" fill="black" /></svg>'
expect "render svg CLI" 'scene{ point(0, 0) }' emit=render:svg

EXPECT='coverage: 2/3 supported[kernel=sympy]
unmapped: root.args[1] unknown_fn kernel=sympy'
expect "coverage CLI" 'sin(x) + unknown_fn(y)' emit=coverage:sympy

EXPECT='agreement: Ok[by=structural, strength=intrinsic, detail=same_tree]'
expect "structural compare CLI" 'x + 1' 'compare=x + 1' by=structural

EXPECT='agreement: Ok[by=simplify, strength=transformer, detail=same_tree_precheck]'
expect "simplify compare CLI" 'x + x' 'compare=x + x' by=simplify

EXPECT='(do (while (> x 0) (do (assign x (- x 1)))) (return x))'
expect "layout do CLI" 'do:
    while x > 0:
        x <- x - 1
    return x
' emit=core

printf 'All Facet CLI smoke tests passed\n'
