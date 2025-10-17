#!/bin/bash

# Binary Size Analysis Script for radiant.exe
# This script systematically analyzes the contribution of each library to the final binary size

echo "🔍 Binary Size Analysis for radiant.exe"
echo "======================================="

# Get baseline size
ORIGINAL_SIZE=$(ls -l radiant.exe | awk '{print $5}')
echo "📏 Current binary size: $(numfmt --to=iec --suffix=B $ORIGINAL_SIZE) ($ORIGINAL_SIZE bytes)"

echo ""
echo "📊 Section Analysis:"
echo "==================="
size radiant.exe

echo ""
echo "🔗 Dynamic Library Dependencies:"
echo "================================"
otool -L radiant.exe

echo ""
echo "📦 Static Library Analysis:"
echo "=========================="

# Extract library list from config
echo "Libraries used in radiant target:"
python3 - << 'EOF'
import json

with open('build_lambda_config.json', 'r') as f:
    config = json.load(f)

# Find radiant target
radiant_target = None
for target in config['targets']:
    if target['name'] == 'radiant':
        radiant_target = target
        break

if radiant_target:
    print("Static libraries:")
    for lib in radiant_target['libraries']:
        print(f"  - {lib}")

    # Get library details from main config
    print("\nLibrary details:")
    lib_details = {}
    for lib_info in config['libraries']:
        lib_details[lib_info['name']] = lib_info

    # Check macOS specific libraries too
    if 'platforms' in config and 'macos' in config['platforms']:
        macos_libs = config['platforms']['macos'].get('libraries', [])
        for lib_info in macos_libs:
            lib_details[lib_info['name']] = lib_info

    for lib_name in radiant_target['libraries']:
        if lib_name in lib_details:
            lib_info = lib_details[lib_name]
            lib_path = lib_info.get('lib', 'N/A')
            lib_type = lib_info.get('link', 'N/A')
            description = lib_info.get('description', '')
            print(f"  📚 {lib_name}:")
            print(f"      Path: {lib_path}")
            print(f"      Type: {lib_type}")
            if description:
                print(f"      Description: {description}")

            # Try to get file size if it's a static library
            if lib_type == 'static' and lib_path != 'N/A' and not lib_path.startswith('-'):
                import os
                if os.path.exists(lib_path):
                    size = os.path.getsize(lib_path)
                    print(f"      Size: {size:,} bytes ({size/1024/1024:.1f} MB)")
                else:
                    print(f"      Size: File not found")
            print()
EOF

echo ""
echo "🔬 Symbol Analysis (largest symbols):"
echo "===================================="
nm -n radiant.exe 2>/dev/null | grep -E "^[0-9a-f]+ [A-Z]" | tail -20

echo ""
echo "📈 Estimated Library Contributions:"
echo "=================================="

# Create a function to estimate library sizes
python3 - << 'EOF'
import json
import os

def format_size(size_bytes):
    """Format size in human readable form"""
    for unit in ['B', 'KB', 'MB', 'GB']:
        if size_bytes < 1024.0:
            return f"{size_bytes:.1f} {unit}"
        size_bytes /= 1024.0
    return f"{size_bytes:.1f} TB"

with open('build_lambda_config.json', 'r') as f:
    config = json.load(f)

# Find radiant target
radiant_target = None
for target in config['targets']:
    if target['name'] == 'radiant':
        radiant_target = target
        break

if radiant_target:
    # Get library details
    lib_details = {}
    for lib_info in config['libraries']:
        lib_details[lib_info['name']] = lib_info

    # Check macOS specific libraries too
    if 'platforms' in config and 'macos' in config['platforms']:
        macos_libs = config['platforms']['macos'].get('libraries', [])
        for lib_info in macos_libs:
            lib_details[lib_info['name']] = lib_info

    total_static_size = 0
    library_sizes = []

    for lib_name in radiant_target['libraries']:
        if lib_name in lib_details:
            lib_info = lib_details[lib_name]
            lib_path = lib_info.get('lib', 'N/A')
            lib_type = lib_info.get('link', 'N/A')
            description = lib_info.get('description', '')

            size = 0
            if lib_type == 'static' and lib_path != 'N/A' and not lib_path.startswith('-') and not lib_path.startswith('-framework'):
                if os.path.exists(lib_path):
                    size = os.path.getsize(lib_path)
                    total_static_size += size

            library_sizes.append((lib_name, size, lib_type, description))

    # Sort by size
    library_sizes.sort(key=lambda x: x[1], reverse=True)

    print("Library size contributions (static libraries only):")
    print("=" * 60)
    for lib_name, size, lib_type, description in library_sizes:
        if size > 0:
            percentage = (size / total_static_size) * 100 if total_static_size > 0 else 0
            print(f"{lib_name:20} {format_size(size):>10} ({percentage:5.1f}%) {lib_type}")
            if description:
                print(f"{'':22} {description}")

    print("=" * 60)
    print(f"{'Total static libs:':20} {format_size(total_static_size):>10}")

    # Estimate other contributions
    binary_size = os.path.getsize('radiant.exe')
    code_size = binary_size - total_static_size
    code_percentage = (code_size / binary_size) * 100
    static_percentage = (total_static_size / binary_size) * 100

    print(f"{'Total binary:':20} {format_size(binary_size):>10}")
    print(f"{'Estimated code:':20} {format_size(code_size):>10} ({code_percentage:5.1f}%)")
    print(f"{'Static libs:':20} {format_size(total_static_size):>10} ({static_percentage:5.1f}%)")

EOF
