#!/usr/bin/env python3
"""
typemeta_extract_json.py - Extract type metadata using clang -ast-dump=json

This is a simpler alternative to the LibTooling-based extractor.
It uses clang's JSON AST dump feature which is available in any clang installation.

Usage:
    # Basic usage
    python3 typemeta_extract_json.py lambda/lambda.h -o generated/typemeta_defs.c

    # With include paths
    python3 typemeta_extract_json.py lambda/lambda.h -I. -Iinclude -o generated/typemeta_defs.c

    # Filter types with regex
    python3 typemeta_extract_json.py lambda/lambda.h --filter "String|List|Map" -o typemeta.c

    # Output JSON instead of C
    python3 typemeta_extract_json.py lambda/lambda.h --json -o types.json

    # Verbose output
    python3 typemeta_extract_json.py lambda/lambda.h -v

Requirements:
    - clang (any version with -ast-dump=json support, clang 9+)
    - Python 3.7+
"""

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

# =============================================================================
# Type Information Structures
# =============================================================================

@dataclass
class FieldInfo:
    name: str
    type_name: str
    offset: int = 0
    bit_offset: int = 0
    bit_width: int = 0
    array_size: int = 0
    is_pointer: bool = False
    is_array: bool = False
    is_bitfield: bool = False
    is_const: bool = False
    is_flex_array: bool = False
    count_field: str = ""
    flags: List[str] = field(default_factory=list)

@dataclass
class EnumValue:
    name: str
    value: int

@dataclass
class TypeInfo:
    name: str
    kind: str  # "struct", "union", "enum"
    size: int = 0
    alignment: int = 0
    fields: List[FieldInfo] = field(default_factory=list)
    enum_values: List[EnumValue] = field(default_factory=list)
    base_type: str = ""
    underlying_type: str = ""
    flags: List[str] = field(default_factory=list)
    source_file: str = ""
    source_line: int = 0

# =============================================================================
# Known Lambda Types (for special handling)
# =============================================================================

REF_COUNTED_TYPES = {
    "String", "Container", "List", "Map", "Element", "Array",
    "ArrayInt", "ArrayInt64", "ArrayFloat", "Range", "Decimal"
}

CONTAINER_TYPES = {
    "List", "Map", "Element", "Array", "ArrayInt", "ArrayInt64", "ArrayFloat"
}

# =============================================================================
# AST Parser
# =============================================================================

class ASTParser:
    """Parse Clang JSON AST dump to extract type information."""

    def __init__(self, verbose: bool = False, filter_pattern: str = None, exclude_pattern: str = None):
        self.verbose = verbose
        self.filter_re = re.compile(filter_pattern) if filter_pattern else None
        self.exclude_re = re.compile(exclude_pattern) if exclude_pattern else None
        self.types: Dict[str, TypeInfo] = {}
        self.type_order: List[str] = []
        self.processed_ids: Set[str] = set()

    def should_include_type(self, name: str) -> bool:
        """Check if type should be included based on filters."""
        if not name:
            return False
        if self.exclude_re and self.exclude_re.match(name):
            return False
        if self.filter_re and not self.filter_re.match(name):
            return False
        return True

    def parse_ast_json(self, ast_json: dict, source_file: str):
        """Parse the root AST node."""
        if "inner" not in ast_json:
            return

        for node in ast_json["inner"]:
            self._process_node(node, source_file)

    def _process_node(self, node: dict, source_file: str):
        """Process a single AST node."""
        kind = node.get("kind", "")

        if kind == "RecordDecl":
            self._process_record(node, source_file)
        elif kind == "EnumDecl":
            self._process_enum(node, source_file)
        elif kind == "TypedefDecl":
            self._process_typedef(node, source_file)

    def _get_location(self, node: dict) -> Tuple[str, int]:
        """Extract source location from node."""
        loc = node.get("loc", {})
        file = loc.get("file", "") or loc.get("includedFrom", {}).get("file", "")
        line = loc.get("line", 0) or loc.get("expansionLoc", {}).get("line", 0)
        return file, line

    def _process_record(self, node: dict, source_file: str):
        """Process a struct/union definition."""
        # Skip forward declarations (no "inner" with fields)
        if "completeDefinition" not in node or not node["completeDefinition"]:
            return

        name = node.get("name", "")
        if not self.should_include_type(name):
            return

        # Skip if already processed
        node_id = node.get("id", "")
        if node_id in self.processed_ids:
            return
        self.processed_ids.add(node_id)

        if name in self.types:
            return

        # Determine struct vs union
        tag_kind = node.get("tagUsed", "struct")

        info = TypeInfo(
            name=name,
            kind="union" if tag_kind == "union" else "struct"
        )

        # Get source location
        file, line = self._get_location(node)
        info.source_file = file or source_file
        info.source_line = line

        # Process fields
        if "inner" in node:
            offset = 0
            for field_node in node["inner"]:
                if field_node.get("kind") == "FieldDecl":
                    field_info = self._process_field(field_node, offset)
                    if field_info:
                        info.fields.append(field_info)
                        # Rough offset estimation (clang doesn't give us exact offsets in JSON)
                        offset += self._estimate_size(field_info.type_name)

        # Add type flags
        if name in REF_COUNTED_TYPES:
            info.flags.append("TYPE_FLAG_REFCOUNTED")
        if name in CONTAINER_TYPES:
            info.flags.append("TYPE_FLAG_CONTAINER")

        # Detect base type from field pattern
        if len(info.fields) >= 3:
            if (info.fields[0].name == "type_id" and
                info.fields[1].name == "flags" and
                info.fields[2].name == "ref_cnt"):
                info.base_type = "Container"

        self.types[name] = info
        self.type_order.append(name)

        if self.verbose:
            print(f"Extracted: {info.kind} {name} ({len(info.fields)} fields)", file=sys.stderr)

    def _process_field(self, node: dict, estimated_offset: int) -> Optional[FieldInfo]:
        """Process a field declaration."""
        name = node.get("name", "")
        if not name:
            return None

        type_info = node.get("type", {})
        type_name = type_info.get("qualType", "unknown")

        field_info = FieldInfo(
            name=name,
            type_name=type_name,
            offset=estimated_offset
        )

        # Check for pointer
        if "*" in type_name:
            field_info.is_pointer = True
            field_info.flags.append("FIELD_FLAG_POINTER")
            field_info.flags.append("FIELD_FLAG_NULLABLE")

            # Heuristic for owned pointers
            if "items" in name or "data" in name:
                field_info.flags.append("FIELD_FLAG_OWNED")

        # Check for array
        if "[" in type_name:
            field_info.is_array = True
            field_info.flags.append("FIELD_FLAG_ARRAY")

            # Extract array size
            match = re.search(r'\[(\d+)\]', type_name)
            if match:
                field_info.array_size = int(match.group(1))
            elif "[]" in type_name:
                field_info.is_flex_array = True
                field_info.flags.append("FIELD_FLAG_FLEX")

        # Check for const
        if "const " in type_name:
            field_info.is_const = True
            field_info.flags.append("FIELD_FLAG_CONST")

        # Check for bitfield
        if "isBitfield" in node and node["isBitfield"]:
            field_info.is_bitfield = True
            field_info.flags.append("FIELD_FLAG_BITFIELD")
            # Clang AST dump includes this in some versions
            if "bitWidth" in node:
                field_info.bit_width = node["bitWidth"]

        # Heuristic: detect count fields
        if field_info.is_pointer:
            if name == "items" or name == "data":
                field_info.count_field = "length"

        return field_info

    def _process_enum(self, node: dict, source_file: str):
        """Process an enum definition."""
        name = node.get("name", "")
        if not self.should_include_type(name):
            return

        node_id = node.get("id", "")
        if node_id in self.processed_ids:
            return
        self.processed_ids.add(node_id)

        if name in self.types:
            return

        info = TypeInfo(name=name, kind="enum")

        # Get source location
        file, line = self._get_location(node)
        info.source_file = file or source_file
        info.source_line = line

        # Get underlying type
        if "fixedUnderlyingType" in node:
            info.underlying_type = node["fixedUnderlyingType"].get("qualType", "int")
        else:
            info.underlying_type = "int"

        # Process enum values
        if "inner" in node:
            for const_node in node["inner"]:
                if const_node.get("kind") == "EnumConstantDecl":
                    ev = EnumValue(
                        name=const_node.get("name", ""),
                        value=self._get_enum_value(const_node)
                    )
                    info.enum_values.append(ev)

        self.types[name] = info
        self.type_order.append(name)

        if self.verbose:
            print(f"Extracted: enum {name} ({len(info.enum_values)} values)", file=sys.stderr)

    def _get_enum_value(self, node: dict) -> int:
        """Extract enum constant value."""
        # Try direct value
        if "inner" in node:
            for inner in node["inner"]:
                if inner.get("kind") == "ConstantExpr":
                    if "value" in inner:
                        try:
                            return int(inner["value"])
                        except (ValueError, TypeError):
                            pass
                elif inner.get("kind") == "IntegerLiteral":
                    if "value" in inner:
                        try:
                            return int(inner["value"])
                        except (ValueError, TypeError):
                            pass
        return 0

    def _process_typedef(self, node: dict, source_file: str):
        """Process a typedef declaration."""
        name = node.get("name", "")
        if not name:
            return

        type_info = node.get("type", {})
        underlying = type_info.get("qualType", "")

        # Handle typedef to struct/union
        # This creates an alias that we can use
        if underlying.startswith("struct "):
            underlying_name = underlying[7:].strip()
            if underlying_name in self.types and name != underlying_name:
                if self.verbose:
                    print(f"Typedef: {name} -> {underlying_name}", file=sys.stderr)
        elif underlying.startswith("enum "):
            underlying_name = underlying[5:].strip()
            if underlying_name in self.types and name != underlying_name:
                if self.verbose:
                    print(f"Typedef: {name} -> {underlying_name}", file=sys.stderr)

    def _estimate_size(self, type_name: str) -> int:
        """Estimate size of a type (rough)."""
        # Strip qualifiers
        clean = type_name.replace("const ", "").replace("volatile ", "").strip()

        # Pointers
        if "*" in clean:
            return 8

        # Arrays
        match = re.search(r'\[(\d+)\]', clean)
        if match:
            array_size = int(match.group(1))
            base_type = re.sub(r'\[\d+\]', '', clean).strip()
            return array_size * self._estimate_size(base_type)

        # Common types
        sizes = {
            "char": 1, "signed char": 1, "unsigned char": 1,
            "int8_t": 1, "uint8_t": 1,
            "short": 2, "unsigned short": 2, "int16_t": 2, "uint16_t": 2,
            "int": 4, "unsigned int": 4, "int32_t": 4, "uint32_t": 4,
            "long": 8, "unsigned long": 8, "long long": 8, "unsigned long long": 8,
            "int64_t": 8, "uint64_t": 8, "size_t": 8,
            "float": 4, "double": 8,
            "_Bool": 1, "bool": 1,
        }

        for type_key, size in sizes.items():
            if clean == type_key:
                return size

        # Default for unknown struct/union
        return 8

# =============================================================================
# Code Generation
# =============================================================================

def compute_type_id(name: str) -> int:
    """Compute FNV-1a hash for type ID."""
    hash_val = 0x811c9dc5
    for c in name:
        hash_val ^= ord(c)
        hash_val = (hash_val * 0x01000193) & 0xFFFFFFFF
    return hash_val

def sanitize_name(name: str) -> str:
    """Convert name to valid C identifier."""
    return re.sub(r'[^a-zA-Z0-9_]', '_', name)

def generate_c_code(types: Dict[str, TypeInfo], type_order: List[str], source_files: List[str]) -> str:
    """Generate C code for type metadata."""
    out = []

    # Header
    out.append("// =============================================================================")
    out.append("// Auto-generated by typemeta_extract_json.py - DO NOT EDIT")
    out.append("// =============================================================================")
    out.append("//")
    out.append("// Generated from:")
    for f in source_files:
        out.append(f"//   {f}")
    out.append("//")
    out.append("// Regenerate with:")
    out.append("//   python3 utils/typemeta_extract_json.py <headers> -o <output>")
    out.append("//")
    out.append("")
    out.append('#include "typemeta.h"')
    out.append("")
    out.append("#ifdef __cplusplus")
    out.append('extern "C" {')
    out.append("#endif")
    out.append("")

    # Primitives
    out.append("// =============================================================================")
    out.append("// Primitive Types")
    out.append("// =============================================================================")
    out.append("")

    primitives = [
        ("void", "TYPE_KIND_VOID", "char"),
        ("bool", "TYPE_KIND_BOOL", "bool"),
        ("char", "TYPE_KIND_CHAR", "char"),
        ("int8", "TYPE_KIND_INT8", "int8_t"),
        ("int16", "TYPE_KIND_INT16", "int16_t"),
        ("int32", "TYPE_KIND_INT32", "int32_t"),
        ("int64", "TYPE_KIND_INT64", "int64_t"),
        ("uint8", "TYPE_KIND_UINT8", "uint8_t"),
        ("uint16", "TYPE_KIND_UINT16", "uint16_t"),
        ("uint32", "TYPE_KIND_UINT32", "uint32_t"),
        ("uint64", "TYPE_KIND_UINT64", "uint64_t"),
        ("float", "TYPE_KIND_FLOAT", "float"),
        ("double", "TYPE_KIND_DOUBLE", "double"),
    ]

    for name, kind, ctype in primitives:
        type_id = compute_type_id(name)
        if name == "void":
            out.append(f"const TypeMeta TYPEMETA_{name} = {{ \"{name}\", {kind}, 0, 1, 0x{type_id:08x}, 0 }};")
        else:
            out.append(f"const TypeMeta TYPEMETA_{name} = {{ \"{name}\", {kind}, sizeof({ctype}), _Alignof({ctype}), 0x{type_id:08x}, 0 }};")
    out.append("")

    # Composite types
    out.append("// =============================================================================")
    out.append("// Composite Types")
    out.append("// =============================================================================")
    out.append("")

    for name in type_order:
        info = types[name]
        safe_name = sanitize_name(name)
        type_id = compute_type_id(name)

        out.append(f"// {info.kind} {name}")
        if info.source_file:
            out.append(f"// from {info.source_file}:{info.source_line}")

        if info.kind == "enum":
            # Enum values array
            if info.enum_values:
                out.append(f"static const EnumValueMeta _typemeta_values_{safe_name}[] = {{")
                for ev in info.enum_values:
                    out.append(f'    {{ "{ev.name}", {ev.value} }},')
                out.append("};")
                out.append("")

            # Underlying type reference
            underlying_ref = "&TYPEMETA_int32"
            if "uint8" in info.underlying_type.lower():
                underlying_ref = "&TYPEMETA_uint8"
            elif "uint16" in info.underlying_type.lower():
                underlying_ref = "&TYPEMETA_uint16"
            elif "uint32" in info.underlying_type.lower():
                underlying_ref = "&TYPEMETA_uint32"

            # Enum TypeMeta
            out.append(f"const TypeMeta TYPEMETA_{safe_name} = {{")
            out.append(f'    .name = "{name}",')
            out.append(f"    .kind = TYPE_KIND_ENUM,")
            out.append(f"    .size = sizeof({name}),")
            out.append(f"    .alignment = _Alignof({name}),")
            out.append(f"    .type_id = 0x{type_id:08x},")
            out.append(f"    .flags = 0,")
            if info.enum_values:
                out.append(f"    .enum_info = {{")
                out.append(f"        .values = _typemeta_values_{safe_name},")
                out.append(f"        .value_count = sizeof(_typemeta_values_{safe_name}) / sizeof(EnumValueMeta),")
                out.append(f"        .underlying_type = {underlying_ref},")
                out.append(f"    }},")
            out.append("};")
        else:
            # Struct/union
            # Fields array
            if info.fields:
                out.append(f"static const FieldMeta _typemeta_fields_{safe_name}[] = {{")
                for fld in info.fields:
                    # Get type reference
                    type_ref = get_field_type_ref(fld.type_name, types)
                    flags = " | ".join(fld.flags) if fld.flags else "0"

                    out.append("    {")
                    out.append(f'        .name = "{fld.name}",')
                    out.append(f"        .type = {type_ref},")
                    out.append(f"        .offset = offsetof({name}, {fld.name}),")
                    out.append(f"        .bit_offset = {fld.bit_offset},")
                    out.append(f"        .bit_width = {fld.bit_width},")
                    out.append(f"        .flags = {flags},")
                    out.append(f"        .array_count = {fld.array_size},")
                    if fld.count_field:
                        out.append(f'        .count_field = "{fld.count_field}",')
                    else:
                        out.append(f"        .count_field = NULL,")
                    out.append("    },")
                out.append("};")
                out.append("")

            # Struct TypeMeta
            kind = "TYPE_KIND_UNION" if info.kind == "union" else "TYPE_KIND_STRUCT"
            flags = " | ".join(info.flags) if info.flags else "0"

            out.append(f"const TypeMeta TYPEMETA_{safe_name} = {{")
            out.append(f'    .name = "{name}",')
            out.append(f"    .kind = {kind},")
            out.append(f"    .size = sizeof({name}),")
            out.append(f"    .alignment = _Alignof({name}),")
            out.append(f"    .type_id = 0x{type_id:08x},")
            out.append(f"    .flags = {flags},")
            if info.fields:
                base_ref = "NULL"
                if info.base_type:
                    base_ref = f"&TYPEMETA_{sanitize_name(info.base_type)}"
                out.append(f"    .composite = {{")
                out.append(f"        .fields = _typemeta_fields_{safe_name},")
                out.append(f"        .field_count = sizeof(_typemeta_fields_{safe_name}) / sizeof(FieldMeta),")
                out.append(f"        .base_type = {base_ref},")
                out.append(f"    }},")
            out.append("};")

        out.append("")

    # Registration function
    out.append("// =============================================================================")
    out.append("// Type Registration")
    out.append("// =============================================================================")
    out.append("")
    out.append("void typemeta_register_generated(void) {")
    out.append("    // Primitives")
    for name, _, _ in primitives:
        out.append(f"    typemeta_register(&TYPEMETA_{name});")
    out.append("")
    out.append("    // Generated types")
    for name in type_order:
        out.append(f"    typemeta_register(&TYPEMETA_{sanitize_name(name)});")
    out.append("}")

    # Footer
    out.append("")
    out.append("#ifdef __cplusplus")
    out.append("}")
    out.append("#endif")

    return "\n".join(out)

def get_field_type_ref(type_name: str, known_types: Dict[str, TypeInfo]) -> str:
    """Get TypeMeta reference for a field type."""
    # Strip qualifiers
    clean = type_name.replace("const ", "").replace("volatile ", "").strip()

    # Handle pointers - use NULL for now (would need pointer type declarations)
    if "*" in clean:
        return "NULL  // pointer type"

    # Handle arrays
    if "[" in clean:
        base = re.sub(r'\[.*\]', '', clean).strip()
        return get_field_type_ref(base, known_types)

    # Primitive type mapping
    primitive_map = {
        "void": "void",
        "bool": "bool", "_Bool": "bool",
        "char": "char", "signed char": "int8", "unsigned char": "uint8",
        "short": "int16", "unsigned short": "uint16",
        "int": "int32", "unsigned int": "uint32", "unsigned": "uint32",
        "long": "int64", "unsigned long": "uint64",
        "long long": "int64", "unsigned long long": "uint64",
        "int8_t": "int8", "uint8_t": "uint8",
        "int16_t": "int16", "uint16_t": "uint16",
        "int32_t": "int32", "uint32_t": "uint32",
        "int64_t": "int64", "uint64_t": "uint64",
        "size_t": "uint64",
        "float": "float", "double": "double",
    }

    if clean in primitive_map:
        return f"&TYPEMETA_{primitive_map[clean]}"

    # Remove struct/enum/union prefix
    if clean.startswith("struct "):
        clean = clean[7:]
    elif clean.startswith("enum "):
        clean = clean[5:]
    elif clean.startswith("union "):
        clean = clean[6:]

    # Check if it's a known type
    if clean in known_types:
        return f"&TYPEMETA_{sanitize_name(clean)}"

    return "NULL  // unknown type"

def generate_json(types: Dict[str, TypeInfo], type_order: List[str]) -> str:
    """Generate JSON output."""
    result = {
        "types": []
    }

    for name in type_order:
        info = types[name]
        type_data = {
            "name": info.name,
            "kind": info.kind,
            "size": info.size,
            "alignment": info.alignment,
            "type_id": f"0x{compute_type_id(name):08x}"
        }

        if info.source_file:
            type_data["source"] = f"{info.source_file}:{info.source_line}"

        if info.fields:
            type_data["fields"] = [
                {
                    "name": f.name,
                    "type": f.type_name,
                    "offset": f.offset,
                    "is_pointer": f.is_pointer,
                    "is_array": f.is_array,
                    "is_bitfield": f.is_bitfield,
                    **({"bit_width": f.bit_width} if f.is_bitfield else {}),
                    **({"array_size": f.array_size} if f.array_size else {}),
                }
                for f in info.fields
            ]

        if info.enum_values:
            type_data["values"] = [
                {"name": ev.name, "value": ev.value}
                for ev in info.enum_values
            ]

        if info.flags:
            type_data["flags"] = info.flags

        result["types"].append(type_data)

    return json.dumps(result, indent=2)

# =============================================================================
# Main
# =============================================================================

def run_clang_ast_dump(header_file: str, include_paths: List[str], extra_args: List[str]) -> dict:
    """Run clang -ast-dump=json and return parsed JSON."""
    cmd = ["clang", "-Xclang", "-ast-dump=json", "-fsyntax-only"]

    # Add include paths
    for inc in include_paths:
        cmd.extend(["-I", inc])

    # Add extra arguments
    cmd.extend(extra_args)

    # Add the header file
    cmd.append(header_file)

    print(f"Running: {' '.join(cmd)}", file=sys.stderr)

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        return json.loads(result.stdout)
    except subprocess.CalledProcessError as e:
        print(f"Error running clang: {e.stderr}", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"Error parsing JSON: {e}", file=sys.stderr)
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(
        description="Extract type metadata from C/C++ headers using clang -ast-dump=json"
    )
    parser.add_argument("headers", nargs="+", help="Header files to process")
    parser.add_argument("-o", "--output", help="Output file (default: stdout)")
    parser.add_argument("-I", "--include", action="append", default=[], help="Include paths")
    parser.add_argument("--filter", help="Regex pattern for types to include")
    parser.add_argument("--exclude", help="Regex pattern for types to exclude")
    parser.add_argument("--json", action="store_true", help="Output JSON instead of C")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    parser.add_argument("--", dest="extra_args", nargs=argparse.REMAINDER, default=[], help="Extra clang arguments")

    args = parser.parse_args()

    # Handle -- separator for extra args
    extra_args = []
    if hasattr(args, 'extra_args') and args.extra_args:
        extra_args = args.extra_args

    # Create parser
    ast_parser = ASTParser(
        verbose=args.verbose,
        filter_pattern=args.filter,
        exclude_pattern=args.exclude
    )

    # Process each header
    for header in args.headers:
        if not os.path.exists(header):
            print(f"Warning: Header not found: {header}", file=sys.stderr)
            continue

        print(f"Processing: {header}", file=sys.stderr)
        ast_json = run_clang_ast_dump(header, args.include, extra_args)
        ast_parser.parse_ast_json(ast_json, header)

    # Generate output
    if args.json:
        output = generate_json(ast_parser.types, ast_parser.type_order)
    else:
        output = generate_c_code(ast_parser.types, ast_parser.type_order, args.headers)

    # Write output
    if args.output:
        os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
        with open(args.output, "w") as f:
            f.write(output)
        print(f"Wrote {len(ast_parser.types)} types to {args.output}", file=sys.stderr)
    else:
        print(output)

    print(f"Extracted {len(ast_parser.types)} types", file=sys.stderr)

if __name__ == "__main__":
    main()
