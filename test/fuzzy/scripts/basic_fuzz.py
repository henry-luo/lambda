#!/usr/bin/env python3
"""
Basic fuzzer for Lambda scripts.

This script generates random Lambda scripts and runs them through the Lambda interpreter,
checking for crashes or other issues.
"""

import os
import sys
import time
import signal
import random
import subprocess
from pathlib import Path
from typing import Optional, Tuple, List, Dict, Any

# Add parent directory to path
sys.path.append(str(Path(__file__).parent.parent))
from grammar_aware.lambda_grammar import LambdaGrammarFuzzer

# Configuration
LAMBDA_BIN = str(Path("../../build/lambda").resolve())
TIMEOUT = 3  # seconds per test
MAX_CRASHES = 10
LOG_DIR = Path("../results/crashes")
LOG_DIR.mkdir(parents=True, exist_ok=True)

class TimeoutError(Exception):
    """Raised when a test times out."""
    pass

def run_lambda(script: str, timeout: int = TIMEOUT) -> Tuple[bool, str, str]:
    """
    Run a Lambda script and return (success, stdout, stderr).
    """
    try:
        proc = subprocess.Popen(
            [LAMBDA_BIN],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        # Set up timeout handler
        def handle_timeout(signum, frame):
            proc.kill()
            raise TimeoutError(f"Test timed out after {timeout} seconds")
        
        signal.signal(signal.SIGALRM, handle_timeout)
        signal.alarm(timeout)
        
        # Run the script
        stdout, stderr = proc.communicate(input=script)
        
        # Disable the alarm
        signal.alarm(0)
        
        return (proc.returncode == 0, stdout, stderr)
        
    except subprocess.TimeoutExpired:
        proc.kill()
        return (False, "", "Timeout")
    except Exception as e:
        return (False, "", str(e))

def save_crash(script: str, stdout: str, stderr: str) -> str:
    """
    Save crash information to a file and return the path.
    """
    crash_id = f"crash_{int(time.time())}_{random.randint(1000, 9999)}"
    crash_dir = LOG_DIR / crash_id
    crash_dir.mkdir(exist_ok=True)
    
    (crash_dir / "script.ls").write_text(script)
    (crash_dir / "stdout.log").write_text(stdout)
    (crash_dir / "stderr.log").write_text(stderr)
    
    return str(crash_dir)

def main():
    """Main fuzzing loop."""
    if not os.path.exists(LAMBDA_BIN):
        print(f"Error: Lambda binary not found at {LAMBDA_BIN}")
        print("Please build Lambda first with: make")
        sys.exit(1)
    
    print(f"[+] Starting fuzzer with Lambda binary: {LAMBDA_BIN}")
    print(f"[+] Timeout per test: {TIMEOUT}s")
    print(f"[+] Crash logs will be saved to: {LOG_DIR.resolve()}")
    
    fuzzer = LambdaGrammarFuzzer()
    stats = {
        'total': 0,
        'success': 0,
        'failures': 0,
        'timeouts': 0,
        'crashes': 0
    }
    
    try:
        while stats['crashes'] < MAX_CRASHES:
            # Generate a random script
            script = fuzzer.generate()
            stats['total'] += 1
            
            # Print status
            if stats['total'] % 10 == 0:
                print(f"\r[+] Tests: {stats['total']} | "
                      f"Success: {stats['success']} | "
                      f"Failures: {stats['failures']} | "
                      f"Timeouts: {stats['timeouts']} | "
                      f"Crashes: {stats['crashes']}", end="")
            
            # Run the script
            success, stdout, stderr = run_lambda(script)
            
            # Update stats
            if 'Timeout' in stderr:
                stats['timeouts'] += 1
                stats['failures'] += 1
                crash_dir = save_crash(script, stdout, stderr)
                print(f"\n[!] Timeout detected! Crash saved to: {crash_dir}")
                stats['crashes'] += 1
            elif not success:
                stats['failures'] += 1
                if stderr.strip():  # Only count as crash if there was an error message
                    stats['crashes'] += 1
                    crash_dir = save_crash(script, stdout, stderr)
                    print(f"\n[!] Crash detected! Crash saved to: {crash_dir}")
                    print(f"[!] Error: {stderr[:200]}...")
            else:
                stats['success'] += 1
                
    except KeyboardInterrupt:
        print("\n\n[!] Fuzzing stopped by user")
    
    # Print final stats
    print("\n[+] Fuzzing completed!")
    print(f"    Total tests: {stats['total']}")
    print(f"    Success: {stats['success']} ({(stats['success']/stats['total'])*100:.1f}%)")
    print(f"    Failures: {stats['failures']} ({(stats['failures']/stats['total'])*100:.1f}%)")
    print(f"    Timeouts: {stats['timeouts']} ({(stats['timeouts']/stats['total'])*100:.1f}%)")
    print(f"    Crashes: {stats['crashes']} ({(stats['crashes']/stats['total'])*100:.1f}%)")
    
    if stats['crashes'] > 0:
        print(f"\n[!] Crash logs saved to: {LOG_DIR.resolve()}")

if __name__ == "__main__":
    main()
