# Mathematical Analysis Report

## Introduction

Mathematical analysis forms the foundation of modern calculus and provides essential tools for understanding continuous functions. This document explores several key concepts including limits, derivatives, and integrals.

## Fundamental Theorems

### The Fundamental Theorem of Calculus

The fundamental theorem of calculus establishes the relationship between differentiation and integration. It states that if $f$ is continuous on the interval $[a,b]$ and $F$ is an antiderivative of $f$, then:

$$\int_a^b f(x) \, dx = F(b) - F(a)$$

This remarkable result shows that integration and differentiation are inverse operations.

### L'Hôpital's Rule

When evaluating limits of indeterminate forms, L'Hôpital's rule provides a powerful technique. If $\lim_{x \to c} f(x) = \lim_{x \to c} g(x) = 0$ or $\pm\infty$, then:

$$\lim_{x \to c} \frac{f(x)}{g(x)} = \lim_{x \to c} \frac{f'(x)}{g'(x)}$$

provided the latter limit exists.

## Series and Convergence

### Power Series

A power series centered at $a$ has the form:

$$\sum_{n=0}^{\infty} c_n (x-a)^n = c_0 + c_1(x-a) + c_2(x-a)^2 + \cdots$$

The radius of convergence $R$ can be found using the ratio test: if $\lim_{n \to \infty} \left|\frac{c_{n+1}}{c_n}\right| = L$, then $R = \frac{1}{L}$.

### Taylor Series

The Taylor series of a function $f(x)$ about point $a$ is:

$$f(x) = \sum_{n=0}^{\infty} \frac{f^{(n)}(a)}{n!}(x-a)^n$$

Some important Taylor series include:

- Exponential function: $e^x = \sum_{n=0}^{\infty} \frac{x^n}{n!}$
- Sine function: $\sin x = \sum_{n=0}^{\infty} \frac{(-1)^n x^{2n+1}}{(2n+1)!}$
- Cosine function: $\cos x = \sum_{n=0}^{\infty} \frac{(-1)^n x^{2n}}{(2n)!}$

## Integration Techniques

### Integration by Parts

For functions $u$ and $v$, integration by parts states:

$$\int u \, dv = uv - \int v \, du$$

This technique is particularly useful for products involving logarithmic, inverse trigonometric, or exponential functions.

### Trigonometric Substitution

For integrals involving $\sqrt{a^2 - x^2}$, $\sqrt{a^2 + x^2}$, or $\sqrt{x^2 - a^2}$, trigonometric substitutions can be effective:

1. For $\sqrt{a^2 - x^2}$: use $x = a\sin\theta$
2. For $\sqrt{a^2 + x^2}$: use $x = a\tan\theta$  
3. For $\sqrt{x^2 - a^2}$: use $x = a\sec\theta$

## Multivariable Calculus

### Partial Derivatives

For a function $f(x,y)$ of two variables, the partial derivatives are:

$$\frac{\partial f}{\partial x} = \lim_{h \to 0} \frac{f(x+h,y) - f(x,y)}{h}$$

$$\frac{\partial f}{\partial y} = \lim_{h \to 0} \frac{f(x,y+h) - f(x,y)}{h}$$

### Double Integrals

The double integral of $f(x,y)$ over region $R$ is:

$$\iint_R f(x,y) \, dA = \int_a^b \int_{g_1(x)}^{g_2(x)} f(x,y) \, dy \, dx$$

This can be used to find areas, volumes, and other geometric quantities.

### Green's Theorem

Green's theorem relates line integrals to double integrals:

$$\oint_C (P \, dx + Q \, dy) = \iint_D \left(\frac{\partial Q}{\partial x} - \frac{\partial P}{\partial y}\right) dA$$

where $C$ is a positively oriented, piecewise-smooth, simple closed curve and $D$ is the region bounded by $C$.

## Applications

### Optimization Problems

Many real-world problems involve finding extrema of functions. For a function $f(x,y)$ subject to constraint $g(x,y) = 0$, the method of Lagrange multipliers seeks points where:

$$\nabla f = \lambda \nabla g$$

### Differential Equations

First-order linear differential equations have the form:

$$\frac{dy}{dx} + P(x)y = Q(x)$$

The solution involves an integrating factor $\mu(x) = e^{\int P(x) dx}$:

$$y = \frac{1}{\mu(x)} \left[ \int \mu(x) Q(x) dx + C \right]$$

## Conclusion

Mathematical analysis provides a rigorous foundation for calculus, enabling precise treatment of limits, continuity, derivatives, and integrals. These tools find applications across science, engineering, economics, and many other fields where quantitative analysis is essential.

The beauty of mathematics lies not just in its practical applications, but in the elegant relationships between seemingly disparate concepts. As we have seen, the fundamental theorem of calculus elegantly connects the geometric notion of area under a curve with the algebraic process of finding antiderivatives.

### Summary of Key Formulas

Here are the most important formulas covered in this analysis:

1. **Fundamental Theorem**: $\int_a^b f(x) dx = F(b) - F(a)$
2. **L'Hôpital's Rule**: $\lim_{x \to c} \frac{f(x)}{g(x)} = \lim_{x \to c} \frac{f'(x)}{g'(x)}$
3. **Taylor Series**: $f(x) = \sum_{n=0}^{\infty} \frac{f^{(n)}(a)}{n!}(x-a)^n$
4. **Integration by Parts**: $\int u dv = uv - \int v du$
5. **Green's Theorem**: $\oint_C (P dx + Q dy) = \iint_D \left(\frac{\partial Q}{\partial x} - \frac{\partial P}{\partial y}\right) dA$

*This completes our comprehensive overview of mathematical analysis fundamentals.*
