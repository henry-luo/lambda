# Comprehensive Math Expression Test

This file contains test cases for all supported math expression groups in Lambda's math parser.

## 1. Basic Operators
**Addition:** $a + b + c$
**Subtraction:** $x - y - z$
**Multiplication:** $p \cdot q \times r * s$
**Division:** $\frac{a}{b} / c \div d$
**Power:** $x^2 \cdot y^{n+1}$

## 2. Functions
**Trigonometric:** $\sin x \cos y \tan z$
**Inverse Trig:** $\arcsin a \arccos b \arctan c$
**Hyperbolic:** $\sinh u \cosh v \tanh w$
**Logarithmic:** $\log x \ln y$
**Absolute Value:** $|x| \lvert y \rvert$

## 3. Special Symbols
**Infinity:** $\infty$
**Partial:** $\frac{\partial f}{\partial x}$
**Nabla:** $\nabla \cdot \vec{F}$
**Ell:** $\ell$
**Hbar:** $\hbar$
**Dotless i/j:** $\imath \jmath$

## 4. Fractions and Roots
**Standard Fraction:** $\frac{numerator}{denominator}$
**Display Fraction:** $\dfrac{a}{b}$
**Text Fraction:** $\tfrac{c}{d}$
**Continued Fraction:** $\cfrac{e}{f}$
**Square Root:** $\sqrt{x}$
**Cube Root:** $\sqrt[3]{8}$
**Fourth Root:** $\sqrt[4]{16}$

## 5. Accents and Decorations
**Hat:** $\hat{x} \widehat{xy}$
**Tilde:** $\tilde{y} \widetilde{abc}$
**Bar:** $\bar{z} \overline{expression}$
**Dot:** $\dot{a} \ddot{b}$
**Check:** $\check{c}$
**Breve:** $\breve{d}$
**Vector:** $\vec{v} \overrightarrow{AB}$

## 6. Arrows
**Basic Arrows:** $\to \leftarrow \leftrightarrow$
**Double Arrows:** $\Rightarrow \Leftarrow \Leftrightarrow$
**Maps To:** $f: A \mapsto B$
**Long Arrows:** $\longrightarrow \longleftarrow$

## 7. Big Operators
**Summation:** $\sum_{i=1}^{n} x_i$
**Product:** $\prod_{k=0}^{m} a_k$
**Integration:** $\int_0^1 f(x) dx$
**Big Union:** $\bigcup_{i \in I} A_i$
**Big Intersection:** $\bigcap_{j=1}^n B_j$
**Big Plus:** $\bigoplus_{k} C_k$
**Big Times:** $\bigotimes_i D_i$

## 8. Delimiters
**Parentheses:** $(a + b)$
**Brackets:** $[x, y]$
**Braces:** $\{z \mid z > 0\}$
**Angle Brackets:** $\langle u, v \rangle$
**Absolute Value:** $\lvert w \rvert$
**Norms:** $\lVert x \rVert$
**Floor/Ceiling:** $\lfloor a \rfloor \lceil b \rceil$

## 9. Relations
**Equality:** $a = b \neq c$
**Inequality:** $x < y \leq z \geq w > u$
**Approximation:** $\pi \approx 3.14$
**Congruence:** $a \equiv b \pmod{n}$
**Similarity:** $\sim \simeq$
**Proportional:** $\propto$

## 10. Set Theory
**Set Membership:** $x \in A \notin B$
**Subset:** $A \subset B \subseteq C$
**Union/Intersection:** $A \cup B \cap C$
**Difference:** $A \setminus B$
**Empty Set:** $\emptyset \varnothing$
**Complement:** $A^c$

## 11. Logic
**Logical And:** $p \land q \wedge r$
**Logical Or:** $s \lor t \vee u$
**Negation:** $\neg p \lnot q$
**Implication:** $p \implies q \Rightarrow r$
**Equivalence:** $p \iff q \Leftrightarrow r$
**Quantifiers:** $\forall x \exists y$

## 12. Number Sets
**Natural Numbers:** $\mathbb{N}$
**Integers:** $\mathbb{Z}$
**Rationals:** $\mathbb{Q}$
**Reals:** $\mathbb{R}$
**Complex:** $\mathbb{C}$
**Cardinals:** $\aleph_0 \beth_1$

## 13. Geometry
**Angle:** $\angle ABC$
**Degree:** $90^\circ$
**Triangle:** $\triangle PQR$
**Parallel:** $AB \parallel CD$
**Perpendicular:** $EF \perp GH$
**Similar:** $\triangle ABC \sim \triangle DEF$

## 14. Calculus
**Limit:** $\lim_{x \to 0} \frac{\sin x}{x}$
**Derivative:** $\frac{d}{dx} f(x) = f'(x)$
**Partial Derivative:** $\frac{\partial^2 f}{\partial x \partial y}$
**Integral:** $\int_a^b f(x) dx$
**Double Integral:** $\iint_D f(x,y) dA$
**Gradient:** $\nabla f$

## 15. Algebra
**Determinant:** $\det(A) = |A|$
**Trace:** $\tr(B)$
**Kernel:** $\ker(T)$
**Image:** $\text{im}(S)$
**Rank:** $\text{rank}(M)$
**Dimension:** $\dim(V)$

## 16. Typography
**Bold:** $\mathbf{x} \boldsymbol{\alpha}$
**Italic:** $\mathit{text}$
**Calligraphic:** $\mathcal{L} \mathscr{F}$
**Fraktur:** $\mathfrak{g} \mathfrak{A}$
**Typewriter:** $\mathtt{code}$
**Sans Serif:** $\mathsf{label}$

## 17. Environments - Matrices
**Basic Matrix:** $\begin{matrix} a & b \\ c & d \end{matrix}$
**Parentheses Matrix:** $\begin{pmatrix} 1 & 2 \\ 3 & 4 \end{pmatrix}$
**Brackets Matrix:** $\begin{bmatrix} x & y \\ z & w \end{bmatrix}$
**Small Matrix:** $\begin{smallmatrix} p & q \\ r & s \end{smallmatrix}$

## 18. Environments - Cases
**Piecewise Function:** 
$$f(x) = \begin{cases} 
x^2 & \text{if } x \geq 0 \\
-x^2 & \text{if } x < 0
\end{cases}$$

## 19. Spacing
**Thin Space:** $a\,b$
**Medium Space:** $c\:d$
**Thick Space:** $e\;f$
**Quad Space:** $g\quad h$
**Double Quad:** $i\qquad j$
**Negative Space:** $k\!l$

## 20. Modular Arithmetic
**Modulo:** $a \bmod n$
**Congruence:** $x \equiv y \pmod{m}$
**Binary Modulo:** $a \pmod{b}$

## 21. Circled Operators
**Circled Plus:** $a \oplus b$
**Circled Times:** $c \otimes d$
**Circled Minus:** $e \ominus f$
**Circled Dot:** $g \odot h$

## 22. Boxed Operators
**Boxed Plus:** $i \boxplus j$
**Boxed Times:** $k \boxtimes l$
**Boxed Minus:** $m \boxminus n$
**Boxed Dot:** $o \boxdot p$

## 23. Extended Arrows
**Hook Arrows:** $\hookrightarrow \hookleftarrow$
**Tail Arrows:** $\twoheadrightarrow \twoheadleftarrow$
**Squiggle Arrow:** $\rightsquigarrow$
**Up/Down Arrows:** $\uparrow \downarrow \updownarrow$

## 24. Extended Relations
**Much Less:** $a \ll b$
**Much Greater:** $c \gg d$
**Asymptotic:** $f(x) \asymp g(x)$
**Precedes:** $x \prec y \preceq z$
**Succeeds:** $u \succ v \succeq w$
**Divides:** $p \mid q$
**Does Not Divide:** $r \nmid s$

## Display Math Examples
**Complex Expression:**
$$\int_{-\infty}^{\infty} e^{-x^2} dx = \sqrt{\pi}$$

**Matrix Equation:**
$$\begin{bmatrix} \cos\theta & -\sin\theta \\ \sin\theta & \cos\theta \end{bmatrix} \begin{bmatrix} x \\ y \end{bmatrix} = \begin{bmatrix} x' \\ y' \end{bmatrix}$$

**Limit Definition:**
$$\lim_{h \to 0} \frac{f(x+h) - f(x)}{h} = f'(x)$$

**Series Expansion:**
$$e^x = \sum_{n=0}^{\infty} \frac{x^n}{n!} = 1 + x + \frac{x^2}{2!} + \frac{x^3}{3!} + \cdots$$
