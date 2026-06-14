# FacetIR Examples

This document provides annotated examples covering every major feature of FacetIR. Each example shows the expression in Surface, Strict, Core, and LaTeX projections (plus Object JSON and SymPy where relevant). All examples are drawn from or consistent with the test notebook and the formal specification.

Format used throughout:

```
Surface:  <surface notation>
Strict:   <strict notation>
Core:     <core S-expression>
LaTeX:    <LaTeX string>
```

---

## 1. Arithmetic and Algebra

### 1.1 Simple Addition

```
Surface:  x + 1
Strict:   +(x, 1)
Core:     (+ x 1)
LaTeX:    x + 1
```

### 1.2 Precedence — Multiplication Binds Tighter Than Addition

```
Surface:  x * 2 + 1
Strict:   +(*(x, 2), 1)
Core:     (+ (* x 2) 1)
LaTeX:    x 2 + 1
```

Multiplication in LaTeX is rendered as juxtaposition (a space), not `\times`.

### 1.3 Exponentiation — Right-Associative

```
Surface:  x ^ 2
Strict:   ^(x, 2)
Core:     (^ x 2)
LaTeX:    x^{2}
```

### 1.4 Unary Negation and Negative Integer Literal

```
Surface:  -x + -1
Strict:   +(neg(x), -1)
Core:     (+ (neg x) -1)
LaTeX:    -x + -1
```

`-x` lowers to a `neg` Compound node. `-1` is lexed as a single integer token (the minus sign is attached to the digit, no whitespace between them).

### 1.5 Division

```
Surface:  a / b
Strict:   /(a, b)
Core:     (/ a b)
LaTeX:    \frac{a}{b}
```

Division renders as `\frac` in LaTeX regardless of context.

### 1.6 Rational Literal (Constructed Programmatically)

Rational literals are not parsed from surface notation; they are constructed programmatically via `Arena::rational("3", "4")`. A `Rat` node is an atom whose `text` field stores the string `"3/4"`. All text-based projections print that string directly.

```
Surface:  3/4
Strict:   3/4
Core:     3/4
LaTeX:    3/4
Object:   {"atom":"rat","value":"3/4"}
```

Note: typing `3/4` in surface notation parses as the binary division compound `(/ 3 4)`, not a `Rat` atom. The `Rat` tag is only reachable via the C++ API or the `sympy-srepr` reader (e.g. `Rational(3, 4)`).

### 1.7 Real Literal

```
Surface:  3.14
Strict:   3.14
Core:     3.14
LaTeX:    3.14
```

A real literal is an atom with tag `Real`. The lexer requires at least one digit on each side of the decimal point. The range check prevents `1..2` from being lexed as `1.` followed by `.2`.

### 1.8 String Atom

```
Surface:  "hello"
Strict:   "hello"
Core:     "hello"
```

String atoms carry JSON-escaped content. They are printed verbatim in all text projections.

### 1.9 Assignment

```
Surface:  f := x |-> x ^ 2
Strict:   :=(f, lam(binder(x, _), ^(x, 2)))
Core:     (:= f (lam (binder x _) (^ x 2)))
LaTeX:    f := x \mapsto x^{2}
```

`:=` is a left-associative infix operator at precedence 25. The right operand is a lambda expression.

### 1.10 Equality as a Compound

```
Surface:  x = 1
Strict:   =(x, 1)
Core:     (= x 1)
LaTeX:    x = 1
```

`=` is an operator (precedence 30), not a parser directive. It produces a `(= ...)` Compound node.

---

## 2. Functions and Lambda

### 2.1 Trigonometric Function

```
Surface:  sin(x)
Strict:   sin(x)
Core:     (sin x)
LaTeX:    \sin\left(x\right)
```

### 2.2 Nested Function Calls

```
Surface:  cos(pi * x)
Strict:   cos(*(pi, x))
Core:     (cos (* pi x))
LaTeX:    \cos\left(\pi x\right)
```

`pi` is a Sym atom; the LaTeX printer maps it to `\pi`.

### 2.3 Square Root

```
Surface:  sqrt(x ^ 2)
Strict:   sqrt(^(x, 2))
Core:     (sqrt (^ x 2))
LaTeX:    \sqrt{x^{2}}
```

### 2.4 User-Defined Function Call

```
Surface:  f(a, b)
Strict:   f(a, b)
Core:     (f a b)
LaTeX:    \operatorname{f}\left(a, b\right)
```

Any identifier followed by `(...)` becomes a Compound with that head. Unknown heads fall through to the generic LaTeX compound rule.

### 2.5 Alternative Call Syntax (Curly Braces)

```
Surface:  f{a, b}
Strict:   f(a, b)
Core:     (f a b)
```

`f{a, b}` and `f(a, b)` produce identical ASTs.

### 2.6 Lambda Abstraction

```
Surface:  x |-> x ^ 2
Strict:   lam(binder(x, _), ^(x, 2))
Core:     (lam (binder x _) (^ x 2))
LaTeX:    x \mapsto x^{2}
```

`_` is the Sym atom `_`, used as a domain placeholder meaning "no domain constraint".

### 2.7 Lambda Assigned to a Variable

```
Surface:  f := x |-> x ^ 2
Strict:   :=(f, lam(binder(x, _), ^(x, 2)))
Core:     (:= f (lam (binder x _) (^ x 2)))
LaTeX:    f := x \mapsto x^{2}
```

From the test notebook: this is the `f := x |-> x^2` cell, confirmed output.

### 2.8 Broadcast Application

```
Surface:  f.(a, b)
Strict:   broadcast(f, args(a, b))
Core:     (broadcast f (args a b))
LaTeX:    \operatorname{broadcast}\left(f, \operatorname{args}\left(a, b\right)\right)
```

The `.(` token is a single lexeme. The broadcast form wraps the argument list in an explicit `(args ...)` node.

### 2.9 Composition via Pipe

```
Surface:  f |> g
Strict:   |>(f, g)
Core:     (|> f g)
LaTeX:    f \triangleright g
```

`|>` is left-associative at precedence 10, the lowest binary operator precedence.

---

## 3. Calculus

### 3.1 Definite Integral

From the test notebook (confirmed output):

```
Surface:  int[x : 0..1](sin(pi * x))
Strict:   int(binder(x, range(0, 1)), sin(*(pi, x)))
Core:     (int (binder x (range 0 1)) (sin (* pi x)))
LaTeX:    \int_{0}^{1} \sin\left(\pi x\right)\,dx
```

The binder form `op[var : domain](body)` lowers to `(op (binder var domain) body)`. The range `0..1` lowers to `(range 0 1)`.

### 3.2 Limit — Approach Binder

From the test notebook (confirmed output):

```
Surface:  lim[x -> 0](sin(x) / x)
Strict:   lim(binder(x, approach(0)), /(sin(x), x))
Core:     (lim (binder x (approach 0)) (/ (sin x) x))
LaTeX:    \lim_{x \to 0} \frac{\sin\left(x\right)}{x}
```

The `->` inside `[...]` triggers the approach binder: the domain becomes `(approach target)`.

### 3.3 Finite Sum Over a Range

From the test notebook (confirmed output):

```
Surface:  sum[x : 0..1](x * 2 + 1)
Strict:   sum(binder(x, range(0, 1)), +(*(x, 2), 1))
Core:     (sum (binder x (range 0 1)) (+ (* x 2) 1))
LaTeX:    \sum_{x = 0}^{1} x 2 + 1
```

### 3.4 Finite Sum Over a Named Domain

```
Surface:  sum[k : S](k ^ 2)
Strict:   sum(binder(k, S), ^(k, 2))
Core:     (sum (binder k S) (^ k 2))
LaTeX:    \sum_{k \in S} k^{2}
```

When the domain is not a `range`, the printer uses `\sum_{k \in S}` form.

### 3.5 Finite Product Over a Range

From the test notebook (confirmed output):

```
Surface:  prod[x : 1..n](x)
Strict:   prod(binder(x, range(1, n)), x)
Core:     (prod (binder x (range 1 n)) x)
LaTeX:    \prod_{x = 1}^{n} x
```

### 3.6 Derivative — Single Variable

```
Surface:  diff[x](sin(x))
Strict:   diff(sin(x), x)
Core:     (diff (sin x) x)
LaTeX:    \frac{d^{}}{dx} \sin\left(x\right)
```

`diff[v1, ...](body)` lowers to `(diff body v1 ...)`: the body is the first argument, variables follow. The LaTeX numerator always carries one empty superscript `^{}` per differentiation variable.

### 3.7 Derivative — Repeated Variable (Second Order)

From the test notebook (confirmed output):

```
Surface:  diff[x, x](sin(x))
Strict:   diff(sin(x), x, x)
Core:     (diff (sin x) x x)
LaTeX:    \frac{d^{}^{}}{dxdx} \sin\left(x\right)
```

Two variables → two `^{}` in the numerator. (See §9.6 of the spec for the general rule.)

### 3.8 Mixed Partial Derivative

```
Surface:  diff[x, y](f(x, y))
Strict:   diff(f(x, y), x, y)
Core:     (diff (f x y) x y)
LaTeX:    \frac{d^{}^{}}{dxdy} \operatorname{f}\left(x, y\right)
```

---

## 4. Logic and Quantifiers

### 4.1 Equality

```
Surface:  x = 1
Strict:   =(x, 1)
Core:     (= x 1)
LaTeX:    x = 1
```

### 4.2 Not-Equal

```
Surface:  a != b
Strict:   !=(a, b)
Core:     (!= a b)
LaTeX:    a \ne b
```

### 4.3 Inequality Comparisons

```
Surface:  x >= 0
Strict:   >=(x, 0)
Core:     (>= x 0)
LaTeX:    x \ge 0

Surface:  x <= n
Strict:   <=(x, n)
Core:     (<= x n)
LaTeX:    x \le n
```

### 4.4 Universal Quantifier

From the test notebook (confirmed output):

```
Surface:  forall[x : R](x ^ 2 >= 0)
Strict:   forall(binder(x, R), >=(^(x, 2), 0))
Core:     (forall (binder x R) (>= (^ x 2) 0))
LaTeX:    \forall x \in R,\; x^{2} \ge 0
```

### 4.5 Existential Quantifier

```
Surface:  exists[y : N](y > 0)
Strict:   exists(binder(y, N), >(y, 0))
Core:     (exists (binder y N) (> y 0))
LaTeX:    \exists y \in N,\; y > 0
```

### 4.6 Implication

```
Surface:  A -> B
Strict:   ->(A, B)
Core:     (-> A B)
LaTeX:    A \to B
```

`->` is right-associative at precedence 45.

### 4.7 Logical Implication (Double Arrow)

```
Surface:  P => Q
Strict:   =>(P, Q)
Core:     (=> P Q)
LaTeX:    P \Rightarrow Q
```

### 4.8 Rewrite Arrow

```
Surface:  pattern ~> replacement
Strict:   ~>(pattern, replacement)
Core:     (~> pattern replacement)
LaTeX:    pattern \to replacement
```

### 4.9 Triple Equality (Definitional / Structural)

```
Surface:  a === b
Strict:   ===(a, b)
Core:     (=== a b)
LaTeX:    a \equiv b
```

### 4.10 Approximate Equality

```
Surface:  f ~= g
Strict:   ~=(f, g)
Core:     (~= f g)
LaTeX:    f \sim g
```

---

## 5. Sets

### 5.1 Set Literal

```
Surface:  { 1, 2, 3 }
Strict:   set(1, 2, 3)
Core:     (set 1 2 3)
LaTeX:    \operatorname{set}\left(1, 2, 3\right)
```

The `set` compound has no dedicated LaTeX rule; it falls through to the generic fallback. Use `setbuild` (§5.3–5.6) when display-quality LaTeX braces are needed.

### 5.2 Set Literal with Expressions

```
Surface:  { x ^ 2, x ^ 3 }
Strict:   set(^(x, 2), ^(x, 3))
Core:     (set (^ x 2) (^ x 3))
LaTeX:    \operatorname{set}\left(x^{2}, x^{3}\right)
```

### 5.3 Set-Builder Without Condition

```
Surface:  { x ^ 2 | x : N }
Strict:   setbuild(^(x, 2), binder(x, N))
Core:     (setbuild (^ x 2) (binder x N))
LaTeX:    \left\{ x^{2} \mid x \in N \right\}
```

### 5.4 Set-Builder With Range Domain

```
Surface:  { x ^ 2 | x : 1..n }
Strict:   setbuild(^(x, 2), binder(x, range(1, n)))
Core:     (setbuild (^ x 2) (binder x (range 1 n)))
LaTeX:    \left\{ x^{2} \mid x = 1,\ldots,n \right\}
```

### 5.5 Set-Builder With Condition

From the test notebook (confirmed output):

```
Surface:  { i ^ 2 | i : 1..n, i > 0 }
Strict:   setbuild(^(i, 2), binder(i, range(1, n)), when=>(i, 0))
Core:     (setbuild (^ i 2) (binder i (range 1 n)) :when (> i 0))
LaTeX:    \left\{ i^{2} \mid i = 1,\ldots,n,\; i > 0 \right\}
```

The condition after the comma becomes the `:when` attribute of the `setbuild` node.

### 5.6 Set-Builder With Named Domain and Condition

```
Surface:  { f(x) | x : S, f(x) != 0 }
Strict:   setbuild(f(x), binder(x, S), when!=(f(x), 0))
Core:     (setbuild (f x) (binder x S) :when (!= (f x) 0))
LaTeX:    \left\{ \operatorname{f}\left(x\right) \mid x \in S,\; \operatorname{f}\left(x\right) \ne 0 \right\}
```

---

## 6. Indexed Expressions

### 6.1 Subscript Shorthand

From the test notebook (confirmed output):

```
Surface:  a_k
Strict:   idx(a, down(k))
Core:     (idx a (down k))
LaTeX:    a_{k}
```

The `_` within the identifier token is recognized as a subscript split: the prefix `a` becomes the tensor, the suffix `k` becomes the index.

### 6.2 Multiple Subscripts in an Expression

From the test notebook (confirmed output):

```
Surface:  a_k + T_mu
Strict:   +(idx(a, down(k)), idx(T, down(mu)))
Core:     (+ (idx a (down k)) (idx T (down mu)))
LaTeX:    a_{k} + T_{\mu}
```

The LaTeX printer maps `mu` to `\mu` via the Greek symbol table.

### 6.3 Explicit Down Index

```
Surface:  idx(a, down(k))       [strict read]
Strict:   idx(a, down(k))
Core:     (idx a (down k))
LaTeX:    a_{k}
```

### 6.4 Tensor With Contravariant and Covariant Indices

From the test notebook (confirmed output, read as core):

```
Core input: (idx T (up mu) (down nu))
Surface:  T^mu_nu
Strict:   idx(T, up(mu), down(nu))
Core:     (idx T (up mu) (down nu))
LaTeX:    T^{\mu}_{\nu}
```

When `idx` has a first `(up ...)` argument followed by a `(down ...)` argument, the LaTeX printer emits `^{...}_{...}` in order.

### 6.5 Subscript on an Indexed Compound

```
Surface:  g_mu_nu
Strict:   idx(g, down(mu_nu))
Core:     (idx g (down mu_nu))
```

Note: `mu_nu` is itself a subscript-shorthand identifier if parsed, or a plain symbol depending on context. The outermost `_` split takes the longest prefix.

### 6.6 Indexed Tensor in an Expression

```
Surface:  T^mu_nu * v_mu
Strict:   *(idx(T, up(mu), down(nu)), idx(v, down(mu)))
Core:     (* (idx T (up mu) (down nu)) (idx v (down mu)))
LaTeX:    T^{\mu}_{\nu} \operatorname{idx}\left(v, \operatorname{down}\left(\mu\right)\right)
```

(The subscript shorthand `v_mu` lowers to `idx(v, down(mu))` at parse time.)

---

## 7. Pattern Matching

### 7.1 Single Meta-Variable

```
Surface:  ?a
Strict:   meta(a, kind=one)
Core:     (meta a :kind one)
LaTeX:    ?a
```

`?a` is lexed as an identifier starting with `?`. It lowers to a `meta` node with `:kind one`.

### 7.2 Sequence Meta-Variable

```
Surface:  ?args...
Strict:   meta(args, kind=seq)
Core:     (meta args :kind seq)
LaTeX:    ?args\ldots
```

The `...` suffix is part of the identifier token. A sequence meta-variable matches zero or more arguments.

### 7.3 Optional Sequence Meta-Variable

```
Surface:  ?opts...?
Strict:   meta(opts, kind=seq?)
Core:     (meta opts :kind seq?)
LaTeX:    ?opts\ldots?
```

The `...?` suffix (tried before `...` in the lexer) produces `:kind seq?`.

### 7.4 Rule Declaration

From the test notebook (confirmed output):

```
Surface:  rule pyth: sin(?a) ^ 2 + cos(?a) ^ 2 ~> 1 when ?a > 0
Strict:   rule(pyth, forall(meta(a, kind=one), ~>(+(^(sin(meta(a, kind=one)), 2), ^(cos(meta(a, kind=one)), 2)), 1), when=>(meta(a, kind=one), 0)))
Core:     (rule pyth (forall (meta a :kind one) (~> (+ (^ (sin (meta a :kind one)) 2) (^ (cos (meta a :kind one)) 2)) 1) :when (> (meta a :kind one) 0)))
LaTeX:    \operatorname{rule}_{\mathrm{pyth}}: \sin\left(?a\right)^{2} + \cos\left(?a\right)^{2} \to 1\;\operatorname{when}\;?a > 0
```

Meta-variables are auto-collected left-to-right and prepended as positional arguments to the `forall` node.

### 7.5 Rule Without Condition

```
Surface:  rule double: ?x + ?x ~> 2 * ?x
Strict:   rule(double, forall(meta(x, kind=one), ~>(+(meta(x, kind=one), meta(x, kind=one)), *(2, meta(x, kind=one)))))
Core:     (rule double (forall (meta x :kind one) (~> (+ (meta x :kind one) (meta x :kind one)) (* 2 (meta x :kind one)))))
LaTeX:    \operatorname{rule}_{\mathrm{double}}: ?x + ?x \to 2 ?x
```

### 7.6 Goal Declaration

From the test notebook (confirmed output):

```
Surface:  goal g: exists[?F : Smooth](int[x : 0..1](?F) = pi / 4)
Strict:   goal(g, exists(binder(meta(F, kind=one), Smooth), =(int(binder(x, range(0, 1)), meta(F, kind=one)), /(pi, 4))))
Core:     (goal g (exists (binder (meta F :kind one) Smooth) (= (int (binder x (range 0 1)) (meta F :kind one)) (/ pi 4))))
LaTeX:    \operatorname{goal}_{\mathrm{g}}\left(\exists ?F \in Smooth,\; \int_{0}^{1} ?F\,dx = \frac{\pi}{4}\right)
```

### 7.7 Simple Goal

```
Surface:  goal zero: f(0) = 0
Strict:   goal(zero, =(f(0), 0))
Core:     (goal zero (= (f 0) 0))
LaTeX:    \operatorname{goal}_{\mathrm{zero}}\left(\operatorname{f}\left(0\right) = 0\right)
```

---

## 8. Context Chaining

### 8.1 Single `@assume`

From the test notebook (confirmed output):

```
Surface:  simplify(sqrt(x ^ 2)) @ assume(x >= 0)
Strict:   simplify(sqrt(^(x, 2)), assume=>=(x, 0))
Core:     (simplify (sqrt (^ x 2)) :assume (>= x 0))
LaTeX:    \operatorname{simplify}\left(\sqrt{x^{2}}\right)\;_{[\operatorname{assume}=x \ge 0]}
Object:
  {
    "head": "simplify",
    "args": [{"head": "sqrt", "args": [{"head": "^", "args": [
      {"atom": "sym", "value": "x"}, {"atom": "int", "value": "2"}
    ]}]}],
    "attrs": {
      "assume": {"head": ">=", "args": [
        {"atom": "sym", "value": "x"}, {"atom": "int", "value": "0"}
      ]}
    }
  }
```

The `@ assume(x >= 0)` context form adds `:assume (>= x 0)` as an attribute on the `simplify` Compound node.

### 8.2 Chained `@assume` and `@via`

From the test notebook (confirmed output):

```
Surface:  simplify(sqrt(x ^ 2)) @ assume(x >= 0) @ via(sympy)
Strict:   simplify(sqrt(^(x, 2)), assume=>=(x, 0), via=sympy)
Core:     (simplify (sqrt (^ x 2)) :assume (>= x 0) :via sympy)
LaTeX:    \operatorname{simplify}\left(\sqrt{x^{2}}\right)\;_{[\operatorname{assume}=x \ge 0, \operatorname{via}=sympy]}
```

Each `@` step adds an attribute. Attributes are stored and printed in alphabetical key order (`assume` before `via`).

### 8.3 `@need` Context

```
Surface:  solve(eq) @ need(x)
Strict:   solve(eq, need=x)
Core:     (solve eq :need x)
LaTeX:    \operatorname{solve}\left(eq\right)\;_{[\operatorname{need}=x]}
```

### 8.4 `@via` Only

```
Surface:  expand(f(x)) @ via(sympy)
Strict:   expand(f(x), via=sympy)
Core:     (expand (f x) :via sympy)
LaTeX:    \operatorname{expand}\left(\operatorname{f}\left(x\right)\right)\;_{[\operatorname{via}=sympy]}
```

### 8.5 `@subst` Context

```
Surface:  f(x) @ subst{x, a + 1}
Strict:   subst(f(x), x, +(a, 1))
Core:     (subst (f x) x (+ a 1))
LaTeX:    \operatorname{subst}\left(\operatorname{f}\left(x\right), x, a + 1\right)
```

`subst{...}` does not add an attribute; it wraps the expression in a `subst` Compound node.

### 8.6 Three-Way Chain

```
Surface:  simplify(expr) @ assume(x > 0) @ need(y) @ via(sympy)
Core:     (simplify expr :assume (> x 0) :need y :via sympy)
```

Attributes accumulate left-to-right. The node retains all three attributes sorted alphabetically: `assume`, `need`, `via`.

### 8.7 Fallback — Non-Compound Left Side

When the left side of `@` is not a Compound (e.g. a bare symbol), the fallback produces:

```
Surface:  x @ assume(x > 0)
Core:     (@ x (assume (> x 0)))
```

---

## 9. SymPy Bridge

### 9.1 Emitting SymPy Expression String

The `emit=sympy` mode calls `print_sympy` and produces a string suitable for Python evaluation:

```sh
echo 'int[x : 0..1](sin(pi*x))' | facet emit=sympy
# Integral(sin(pi*x), (x, 0, 1))
```

```sh
echo 'sum[k : 1..n](k ^ 2)' | facet emit=sympy
# Sum(k**2, (k, 1, n))
```

### 9.2 Reading SymPy srepr — Integral

From the test notebook (confirmed output):

```
Input (sympy-srepr):
  Integral(sin(Mul(pi, Symbol('x'))), Tuple(Symbol('x'), Integer(0), Integer(1)))

Surface:  int[x : 0..1](sin(pi * x))
Strict:   int(binder(x, range(0, 1)), sin(*(pi, x)))
Core:     (int (binder x (range 0 1)) (sin (* pi x)))
LaTeX:    \int_{0}^{1} \sin\left(\pi x\right)\,dx
```

### 9.3 Reading SymPy srepr — Derivative

From the test notebook (confirmed output):

```
Input (sympy-srepr):
  Derivative(sin(Symbol('x')), Tuple(Symbol('x'), Integer(2)))

Surface:  diff[x, x](sin(x))
Strict:   diff(sin(x), x, x)
Core:     (diff (sin x) x x)
LaTeX:    \frac{d^{}^{}}{dxdx} \sin\left(x\right)
```

`Tuple(Symbol('x'), Integer(2))` means "differentiate with respect to x twice". The reader expands this to two repetitions of `x` in the `diff` node.

### 9.4 Reading SymPy srepr — Product

From the test notebook (confirmed output):

```
Input (sympy-srepr):
  Product(Symbol('x'), Tuple(Symbol('x'), Integer(1), Symbol('n')))

Surface:  prod[x : 1..n](x)
Strict:   prod(binder(x, range(1, n)), x)
Core:     (prod (binder x (range 1 n)) x)
LaTeX:    \prod_{x = 1}^{n} x
```

### 9.5 Reading SymPy srepr — Add, Mul, Pow

```
Input (sympy-srepr):
  Add(Pow(Symbol('x'), Integer(2)), Integer(1))

Core:     (+ (^ x 2) 1)
Surface:  x ^ 2 + 1
LaTeX:    x^{2} + 1
```

### 9.6 Reading SymPy srepr — Limit

```
Input (sympy-srepr):
  Limit(Mul(Symbol('sin'), Symbol('x')), Symbol('x'), Integer(0))

Core:     (lim (binder x (approach 0)) (* sin x))
Surface:  lim[x -> 0](sin * x)
```

(Note: `sin` here would be read as a bare symbol, not the function; actual SymPy limits over `sin(x)` use the full function form.)

### 9.7 evaluate_sympy — Round-Trip Without Assumption

```sh
echo 'sqrt(x ^ 2)' | facet emit=sympy-core
# (sqrt (^ x 2))   [no simplification without assumption]
```

When `:via sympy` is absent, `evaluate_sympy` performs a `print_sympy_srepr` round-trip without assumption injection.

### 9.8 evaluate_sympy — With `@via(sympy)` and `@assume`

```sh
echo 'simplify(sqrt(x^2)) @ assume(x >= 0) @ via(sympy)' | facet emit=sympy-core
# (x)   [SymPy simplifies sqrt(x^2) to x under x >= 0]
```

Workflow:
1. `evaluate_sympy` detects `:via sympy` on the outer Compound.
2. Strips the context attributes.
3. Extracts `:assume (>= x 0)` → `Symbol('x', nonnegative=True)`.
4. Evaluates `simplify(sqrt(x**2))` in SymPy with the assumption.
5. SymPy returns `x`; `read_sympy_srepr` reads `Symbol('x')` back as the Facet Sym node `x`.

### 9.9 Assumption Mapping

| FacetIR condition | SymPy Symbol keyword  |
|-------------------|-----------------------|
| `(>= sym 0)`      | `nonnegative=True`    |
| `(> sym 0)`       | `positive=True`       |
| `(<= sym 0)`      | `nonpositive=True`    |
| `(< sym 0)`       | `negative=True`       |

---

## 10. Object Projection

### 10.1 Atom — Symbol

```json
{"atom": "sym", "value": "x"}
```

```sh
echo 'x' | facet emit=object
# {"atom":"sym","value":"x"}
```

### 10.2 Atom — Integer

```json
{"atom": "int", "value": "42"}
{"atom": "int", "value": "-1"}
```

Negative integers store the leading `-` as part of the value string.

### 10.3 Atom — Real

```json
{"atom": "real", "value": "3.14"}
```

### 10.4 Atom — String

```json
{"atom": "str", "value": "hello"}
```

### 10.5 Simple Compound

```sh
echo 'x + 1' | facet emit=object
```

```json
{
  "head": "+",
  "args": [
    {"atom": "sym", "value": "x"},
    {"atom": "int", "value": "1"}
  ]
}
```

`"attrs"` is omitted when the attribute map is empty.

### 10.6 Compound With Attributes

From the test notebook (confirmed output):

```sh
echo 'simplify(sqrt(x^2)) @ assume(x >= 0)' | facet emit=object
```

```json
{
  "head": "simplify",
  "args": [
    {
      "head": "sqrt",
      "args": [{"head": "^", "args": [
        {"atom": "sym", "value": "x"},
        {"atom": "int", "value": "2"}
      ]}]
    }
  ],
  "attrs": {
    "assume": {
      "head": ">=",
      "args": [
        {"atom": "sym", "value": "x"},
        {"atom": "int", "value": "0"}
      ]
    }
  }
}
```

The `"attrs"` field appears only when the attribute map is non-empty. Keys are in alphabetical order.

### 10.7 Nested Compound — Integral

From the test notebook (confirmed output):

```sh
echo 'int[x : 0..1](sin(pi*x))' | facet emit=object
```

```json
{
  "head": "int",
  "args": [
    {
      "head": "binder",
      "args": [
        {"atom": "sym", "value": "x"},
        {"head": "range", "args": [
          {"atom": "int", "value": "0"},
          {"atom": "int", "value": "1"}
        ]}
      ]
    },
    {
      "head": "sin",
      "args": [
        {"head": "*", "args": [
          {"atom": "sym", "value": "pi"},
          {"atom": "sym", "value": "x"}
        ]}
      ]
    }
  ]
}
```

### 10.8 Round-Trip Property

The Object projection round-trips exactly: `read_object(print_object(r))` returns the identical interned node (pointer equality). Parsing a JSON object and re-serialising it produces the same canonical JSON (with empty `"attrs"` fields omitted and keys in alphabetical order).

---

## Appendix: Greek Symbol Mapping in LaTeX

The following Sym values are mapped to LaTeX Greek letters by `print_latex`. All other symbols are printed verbatim.

| Sym    | LaTeX       |
|--------|-------------|
| `pi`   | `\pi`       |
| `alpha`| `\alpha`    |
| `beta` | `\beta`     |
| `gamma`| `\gamma`    |
| `delta`| `\delta`    |
| `epsilon`| `\epsilon`|
| `theta`| `\theta`    |
| `lambda`| `\lambda` |
| `mu`   | `\mu`       |
| `nu`   | `\nu`       |
| `sigma`| `\sigma`    |
| `omega`| `\omega`    |
