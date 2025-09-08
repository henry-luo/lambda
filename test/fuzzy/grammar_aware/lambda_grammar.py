import random
import string
from typing import List, Dict, Any, Optional

class LambdaGrammarFuzzer:
    """Grammar-based fuzzer for Lambda scripts."""
    
    def __init__(self):
        # Variables and functions from the grammar
        self.vars = ['x', 'y', 'z', 'a', 'b', 'c', 'i', 'j', 'k', 'n']
        self.funcs = ['sin', 'cos', 'tan', 'sqrt', 'log', 'exp', 'abs', 'round']
        self.types = ['int', 'float', 'string', 'bool', 'array', 'map']
        self.operators = ['+', '-', '*', '/', '%', '^', '==', '!=', '<', '>', '<=', '>=']
        self.keywords = ['if', 'else', 'for', 'while', 'return', 'let', 'in', 'true', 'false']
        
    def gen_number(self) -> str:
        """Generate a random number (int or float)."""
        if random.random() < 0.7:  # 70% chance of integer
            return str(random.randint(-100, 100))
        return f"{random.uniform(-100, 100):.4f}"
    
    def gen_string(self) -> str:
        """Generate a random string literal."""
        length = random.randint(0, 20)
        chars = string.ascii_letters + string.digits + ' '
        return f'"{"".join(random.choice(chars) for _ in range(length))}"'
    
    def gen_bool(self) -> str:
        """Generate a boolean literal."""
        return random.choice(['true', 'false'])
    
    def gen_var(self) -> str:
        """Generate a variable reference."""
        return random.choice(self.vars)
    
    def gen_type(self) -> str:
        """Generate a type annotation."""
        return random.choice(self.types)
    
    def gen_expr(self, depth: int = 0) -> str:
        """Generate a random expression."""
        if depth > 3:  # Limit recursion depth
            return self.gen_simple_expr()
            
        expr_type = random.choices(
            ['number', 'string', 'bool', 'var', 'binary', 'func', 'array', 'if', 'let'],
            weights=[0.2, 0.1, 0.05, 0.2, 0.3, 0.05, 0.05, 0.025, 0.025]
        )[0]
        
        if expr_type == 'number':
            return self.gen_number()
        elif expr_type == 'string':
            return self.gen_string()
        elif expr_type == 'bool':
            return self.gen_bool()
        elif expr_type == 'var':
            return self.gen_var()
        elif expr_type == 'binary':
            op = random.choice(self.operators)
            left = self.gen_expr(depth + 1)
            right = self.gen_expr(depth + 1)
            return f"{left} {op} {right}"
        elif expr_type == 'func':
            func = random.choice(self.funcs)
            args = ", ".join(self.gen_expr(depth + 1) for _ in range(random.randint(1, 3)))
            return f"{func}({args})"
        elif expr_type == 'array':
            elements = [self.gen_expr(depth + 1) for _ in range(random.randint(0, 5))]
            return f"[{", ".join(elements)}]"
        elif expr_type == 'if':
            cond = self.gen_expr(depth + 1)
            then_expr = self.gen_expr(depth + 1)
            else_expr = self.gen_expr(depth + 1)
            return f"if {cond} then {then_expr} else {else_expr} fi"
        else:  # let expression
            var = self.gen_var()
            value = self.gen_expr(depth + 1)
            body = self.gen_expr(depth + 1)
            return f"let {var} = {value} in {body}"
    
    def gen_simple_expr(self) -> str:
        """Generate a simple expression without deep recursion."""
        return random.choice([
            self.gen_number(),
            self.gen_string(),
            self.gen_bool(),
            self.gen_var(),
            f"({self.gen_expr(3)})"
        ])
    
    def generate(self, min_length: int = 10, max_length: int = 1000) -> str:
        """Generate a complete Lambda script."""
        # Start with some variable declarations
        script = []
        num_vars = random.randint(1, 5)
        
        for _ in range(num_vars):
            var = self.gen_var()
            value = self.gen_expr(1)
            script.append(f"let {var} = {value}")
        
        # Add a main expression
        script.append(self.gen_expr(1))
        
        # Join with semicolons and ensure length constraints
        result = ";\n".join(script)
        
        # Ensure the script meets length requirements
        while len(result) < min_length:
            result += ";\n" + self.gen_expr(1)
            
        if len(result) > max_length:
            result = result[:max_length]
            
        return result

# Example usage
if __name__ == "__main__":
    fuzzer = LambdaGrammarFuzzer()
    for _ in range(5):
        print("-" * 40)
        print(fuzzer.generate())
        print()
