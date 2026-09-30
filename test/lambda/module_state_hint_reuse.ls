// D5.4.3: repeated calls into one imported unit share this context's slab.
import a: .mod_same_let_a

[a.framed(), a.framed(), a.framed()]
