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

EXPECT='math.sin(x)**2+math.sqrt(y)'
expect "source python CLI" 'sin(x)^2 + sqrt(y)' emit=source:python

EXPECT='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/p9sAAAAASUVORK5CYII='
expect "render png CLI" 'plot3d[x : 0..1](x^2)' emit=render:png

EXPECT='<!doctype html><html><body><label>t<input type="range" min="0" max="2" value="1" step="any"></label><pre>sin(t * x)</pre></body></html>'
expect "render html manipulate CLI" 'manipulate[t : 0..2](sin(t*x))' emit=render:html

EXPECT='coverage: 2/3 supported[kernel=sympy]
unmapped: root.args[1] unknown_fn kernel=sympy'
expect "coverage CLI" 'sin(x) + unknown_fn(y)' emit=coverage:sympy

EXPECT='agreement: Ok[by=structural, strength=intrinsic, detail=same_tree]'
expect "structural compare CLI" 'x + 1' 'compare=x + 1' by=structural

EXPECT='agreement: Ok[by=simplify, strength=transformer, detail=same_tree_precheck]'
expect "simplify compare CLI" 'x + x' 'compare=x + x' by=simplify

EXPECT='agreement: Ok[by=numeric, strength=evidence, detail=numeric_samples, tol=0, samples=3]'
expect "numeric compare CLI" 'x + 1' 'compare=x + 1' 'by=numeric(samples=3,tol=0)'

EXPECT='agreement: Fail[by=numeric, strength=evidence, detail=numeric_witness, tol=0, samples=2, witness=sample=0,x=-0.889,lhs=0.111,rhs=1.111]'
expect "numeric compare witness CLI" 'x + 1' 'compare=x + 2' 'by=numeric(samples=2,tol=0)'

EXPECT='(do (while (> x 0) (do (assign x (- x 1)))) (return x))'
expect "layout do CLI" 'do:
    while x > 0:
        x <- x - 1
    return x
' emit=core

printf 'All Facet CLI smoke tests passed\n'
