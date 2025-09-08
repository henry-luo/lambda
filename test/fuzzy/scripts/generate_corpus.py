#!/usr/bin/env python3
"""
Generate a corpus of Lambda scripts for fuzzing.
"""

import random
import sys
from pathlib import Path

# Add parent directory to path
sys.path.append(str(Path(__file__).parent.parent))
from grammar_aware.lambda_grammar import LambdaGrammarFuzzer

def generate_corpus(output_dir: Path, count: int = 1000, min_length: int = 10, max_length: int = 5000):
    """
    Generate a corpus of Lambda scripts.
    
    Args:
        output_dir: Directory to save the corpus
        count: Number of scripts to generate
        min_length: Minimum length of each script
        max_length: Maximum length of each script
    """
    output_dir.mkdir(parents=True, exist_ok=True)
    fuzzer = LambdaGrammarFuzzer()
    
    print(f"Generating {count} test cases in {output_dir}...")
    
    for i in range(count):
        # Generate a script with random length in the specified range
        script = fuzzer.generate(
            min_length=min_length,
            max_length=random.randint(min_length, max_length)
        )
        
        # Save to file
        output_file = output_dir / f"test_{i:04d}.ls"
        output_file.write_text(script)
        
        if (i + 1) % 100 == 0:
            print(f"  Generated {i + 1}/{count} test cases...")
    
    print(f"Done! Generated {count} test cases in {output_dir}")

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="Generate a corpus of Lambda scripts for fuzzing.")
    parser.add_argument(
        "-o", "--output", 
        type=Path, 
        default=Path("../corpus"),
        help="Output directory for the corpus"
    )
    parser.add_argument(
        "-n", "--count", 
        type=int, 
        default=1000,
        help="Number of test cases to generate"
    )
    parser.add_argument(
        "--min-length", 
        type=int, 
        default=10,
        help="Minimum length of each script"
    )
    parser.add_argument(
        "--max-length", 
        type=int, 
        default=5000,
        help="Maximum length of each script"
    )
    
    args = parser.parse_args()
    generate_corpus(
        output_dir=args.output,
        count=args.count,
        min_length=args.min_length,
        max_length=args.max_length
    )
