#!/usr/bin/env python3
"""
Premake5 Generator for Lambda Build System
Migrates from shell-based compile.sh to Premake5-based build system
while preserving the existing JSON configuration structure.
"""

import json
import os
import sys
import glob
from pathlib import Path
from typing import Dict, List, Any, Optional

class PremakeGenerator:
    def __init__(self, config_path: str = "build_lambda_config.json"):
        with open(config_path, 'r') as f:
            self.config = json.load(f)
        self.premake_content = []
        self.external_libraries = self._parse_external_libraries()

    def _parse_external_libraries(self) -> Dict[str, Dict[str, str]]:
        """Parse external library definitions from JSON config"""
        libraries = {}
        
        # Parse regular libraries
        for lib in self.config.get('libraries', []):
            if 'lib' in lib:  # Include both static and dynamic libraries
                libraries[lib['name']] = {
                    'include': lib.get('include', ''),
                    'lib': lib.get('lib', ''),
                    'link': lib.get('link', 'static')
                }
        
        # Parse dev_libraries (development/test-only libraries like ginac, cln)
        for lib in self.config.get('dev_libraries', []):
            if 'lib' in lib:  # Include both static and dynamic libraries
                libraries[lib['name']] = {
                    'include': lib.get('include', ''),
                    'lib': lib.get('lib', ''),
                    'link': lib.get('link', 'static')
                }
        
        return libraries

    def parse_config(self) -> Dict[str, Any]:
        """Parse build_lambda_config.json and extract configuration"""
        try:
            return self.config
        except FileNotFoundError:
            print(f"Error: Configuration file not found")
            sys.exit(1)
        except json.JSONDecodeError as e:
            print(f"Error: Invalid JSON: {e}")
            sys.exit(1)
    
    def generate_workspace(self) -> None:
        """Generate the main workspace configuration"""
        output = self.config.get('output', 'lambda.exe')
        startup_project = output.replace('.exe', '')
        
        self.premake_content.extend([
            'workspace "Lambda"',
            '    configurations { "Debug", "Release" }',
            '    platforms { "x64" }',
            '    location "build/premake"',
            f'    startproject "{startup_project}"',
            '    ',
            '    -- Global settings',
            '    cppdialect "C++17"',
            '    cdialect "C99"',
            '    warnings "Extra"',
            '    ',
            '    filter "configurations:Debug"',
            '        defines { "DEBUG" }',
            '        symbols "On"',
            '        optimize "Off"',
            '        buildoptions { "-fsanitize=address", "-fno-omit-frame-pointer" }',
            '        linkoptions { "-fsanitize=address" }',
            '    ',
            '    filter "configurations:Release"',
            '        defines { "NDEBUG" }',
            '        optimize "On"',
            '    ',
            '    filter {}',
            ''
        ])
    
    def generate_library_projects(self) -> None:
        """Generate static library projects from JSON config"""
        libraries = self.config.get('libraries', [])
        
        for lib in libraries:
            lib_name = lib.get('name', '')
            link_type = lib.get('link', 'static')
            
            # Skip external libraries and inline libraries for now
            if link_type in ['dynamic', 'static'] and 'sources' not in lib:
                continue
                
            if link_type == 'inline' and 'sources' in lib:
                self._generate_inline_library(lib)
    
    def _generate_inline_library(self, lib: Dict[str, Any]) -> None:
        """Generate a static library project for inline libraries"""
        lib_name = lib['name']
        sources = lib.get('sources', [])
        source_files = lib.get('source_files', [])
        
        if not sources and not source_files:
            return
            
        self.premake_content.extend([
            f'project "{lib_name}"',
            '    kind "StaticLib"',
            '    language "C"',
            '    targetdir "build/lib"',
            '    objdir "build/obj/%{prj.name}"',
            '    ',
        ])
        
        # Add source files
        all_sources = sources + source_files
        if all_sources:
            self.premake_content.append('    files {')
            for source in all_sources:
                self.premake_content.append(f'        "{source}",')
            self.premake_content.append('    }')
            self.premake_content.append('    ')
        
        # Add include directories
        if 'include' in lib:
            self.premake_content.extend([
                '    includedirs {',
                f'        "{lib["include"]}",',
                '    }',
                '    '
            ])
        
        # Add special build options
        self.premake_content.extend([
            '    buildoptions {',
            '        "-fms-extensions",',
            '        "-fcolor-diagnostics",',
            '        "-pedantic"',
            '    }',
            '    ',
            ''
        ])
    
    def generate_complex_libraries(self) -> None:
        """Generate complex library projects like lambda-runtime-full"""
        targets = self.config.get('targets', [])
        
        for lib in targets:
            lib_name = lib.get('name', '')
            if lib_name in ['lambda-runtime-full', 'lambda-input-full', 'lambda-lib']:
                self._generate_complex_library(lib)
    
    def _generate_complex_library(self, lib: Dict[str, Any]) -> None:
        """Generate complex library with multiple source files and dependencies"""
        lib_name = lib['name']
        # Handle both 'sources' (targets) and 'source_files' (libraries) field names
        source_files = lib.get('source_files', []) or lib.get('sources', [])
        source_patterns = lib.get('source_patterns', [])
        dependencies = lib.get('libraries', [])
        
        # For lambda-lib, it's a meta-library that depends on other inline libraries
        if lib_name == 'lambda-lib':
            self._generate_meta_library(lib)
            return
        
        # Separate C and C++ files
        c_files = [f for f in source_files if f.endswith('.c')]
        cpp_files = [f for f in source_files if f.endswith('.cpp')]
        
        # If we have both C and C++ files, create separate projects
        if c_files and cpp_files:
            # Create C project
            self._create_language_project(lib, c_files, [], dependencies, "C", f"{lib_name}-c")
            # Create C++ project
            self._create_language_project(lib, cpp_files, source_patterns, dependencies, "C++", f"{lib_name}-cpp")
            # Create main project that links both
            self._create_wrapper_project(lib_name, [f"{lib_name}-c", f"{lib_name}-cpp"])
        else:
            # Single language project
            language = "C++" if cpp_files or any('*.cpp' in pattern for pattern in source_patterns) else "C"
            self._create_language_project(lib, source_files, source_patterns, dependencies, language, lib_name)
    
    def _create_language_project(self, lib: Dict[str, Any], source_files: List[str], 
                               source_patterns: List[str], dependencies: List[str], 
                               language: str, project_name: str) -> None:
        """Create a single-language project"""
        self.premake_content.extend([
            f'project "{project_name}"',
            '    kind "StaticLib"',
            f'    language "{language}"',
            '    targetdir "build/lib"',
            '    objdir "build/obj/%{prj.name}"',
            '    ',
        ])
        
        # Add source files
        if source_files:
            self.premake_content.append('    files {')
            for source in source_files:
                self.premake_content.append(f'        "{source}",')
            self.premake_content.append('    }')
            self.premake_content.append('    ')
        
        # Add source patterns
        if source_patterns:
            for pattern in source_patterns:
                self.premake_content.extend([
                    '    files {',
                    f'        "{pattern}",',
                    '    }',
                    '    '
                ])
        
        # Add exclude patterns if specified
        exclude_patterns = lib.get('exclude_patterns', [])
        if exclude_patterns:
            self.premake_content.extend([
                '    removefiles {',
            ])
            for exclude_pattern in exclude_patterns:
                self.premake_content.append(f'        "{exclude_pattern}",')
            self.premake_content.extend([
                '    }',
                '    '
            ])
        
        # Add include directories
        if 'include' in lib:
            self.premake_content.extend([
                '    includedirs {',
                f'        "{lib["include"]}",',
                '        "lambda/tree-sitter/lib/include",',
                '        "lambda/tree-sitter-lambda/bindings/c",',
                '        "/usr/local/include",',
                '        "/opt/homebrew/include",',
                '        "/opt/homebrew/Cellar/criterion/2.4.2_2/include",',
                '        "/opt/homebrew/Cellar/mpdecimal/4.0.1/include",',
                '    }',
                '    '
            ])
        
        # Add build options
        build_opts = ['-fms-extensions', '-fcolor-diagnostics', '-pedantic']
        if language == "C++":
            build_opts.append('-std=c++17')
        
        self.premake_content.extend([
            '    buildoptions {',
        ])
        for i, opt in enumerate(build_opts):
            comma = ',' if i < len(build_opts) - 1 else ''
            self.premake_content.append(f'        "{opt}"{comma}')
        self.premake_content.extend([
            '    }',
            '    '
        ])
        
        # Add library dependencies
        if dependencies:
            # Separate internal and external dependencies
            internal_deps = []
            external_deps = []
            
            for dep in dependencies:
                if dep in self.external_libraries:
                    external_deps.append(dep)
                else:
                    internal_deps.append(dep)
            
            # Add libdirs if we have dependencies
            self.premake_content.extend([
                '    libdirs {',
                '        "/opt/homebrew/lib",',
                '        "/opt/homebrew/Cellar/criterion/2.4.2_2/lib",',
                '        "/usr/local/lib",',
                '        "build/lib",',
                '    }',
                '    '
            ])
            
            # Add linkoptions for external static libraries
            if external_deps:
                static_libs = []
                frameworks = []
                dynamic_libs = []
                
                for dep in external_deps:
                    if dep in self.external_libraries:
                        lib_info = self.external_libraries[dep]
                        lib_path = lib_info['lib']
                        
                        if lib_info.get('link') == 'dynamic':
                            if lib_path.startswith('-framework '):
                                frameworks.append(lib_path.replace('-framework ', ''))
                            elif lib_path.startswith('-l'):
                                dynamic_libs.append(lib_path.replace('-l', ''))
                            else:
                                dynamic_libs.append(lib_path)
                        else:
                            # Static library
                            if not lib_path.startswith('/'):
                                lib_path = f"../../{lib_path}"
                            static_libs.append(lib_path)
                
                # Add static libraries to linkoptions
                if static_libs:
                    self.premake_content.append('    linkoptions {')
                    for lib_path in static_libs:
                        self.premake_content.append(f'        "{lib_path}",')
                    self.premake_content.extend([
                        '    }',
                        '    '
                    ])
                
                # Add frameworks, dynamic libraries, and internal libraries to links
                if frameworks or dynamic_libs or internal_deps:
                    self.premake_content.append('    links {')
                    for framework in frameworks:
                        self.premake_content.append(f'        "{framework}.framework",')
                    for lib in dynamic_libs:
                        self.premake_content.append(f'        "{lib}",')
                    for dep in internal_deps:
                        if dep == 'criterion':
                            self.premake_content.append('        "criterion",')
                        else:
                            self.premake_content.append(f'        "{dep}",')
                    self.premake_content.extend([
                        '    }',
                        '    '
                    ])
            else:
                # Add links for internal libraries only
                if internal_deps:
                    self.premake_content.append('    links {')
                    for dep in internal_deps:
                        if dep == 'criterion':
                            self.premake_content.append('        "criterion",')
                        else:
                            self.premake_content.append(f'        "{dep}",')
                    self.premake_content.extend([
                        '    }',
                        '    '
                    ])
        
        self.premake_content.append('')
    
    def _create_wrapper_project(self, lib_name: str, sub_projects: List[str]) -> None:
        """Create a wrapper project that combines multiple sub-projects"""
        self.premake_content.extend([
            f'project "{lib_name}"',
            '    kind "StaticLib"',
            '    language "C++"',
            '    targetdir "build/lib"',
            '    objdir "build/obj/%{prj.name}"',
            '    ',
            '    -- Wrapper library with empty source file',
            '    files {',
            '        "utils/empty.cpp",',
            '    }',
            '    ',
            '    links {',
        ])
        for source in sub_projects:
            self.premake_content.append(f'        "{source}",')
        self.premake_content.extend([
            '    }',
            '    '
        ])
        self.premake_content.append('')
    
    def _generate_meta_library(self, lib: Dict[str, Any]) -> None:
        """Generate a meta-library that combines other libraries"""
        lib_name = lib['name']
        dependencies = lib.get('libraries', [])
        sources = lib.get('sources', [])
        
        self.premake_content.extend([
            f'project "{lib_name}"',
            '    kind "StaticLib"',
            '    language "C"',
            '    targetdir "build/lib"',
            '    objdir "build/obj/%{prj.name}"',
            '    ',
            '    -- Meta-library: combines source files from dependencies',
            '    files {',
        ])
        
        # Add sources directly specified in the library
        for source in sources:
            self.premake_content.append(f'        "{source}",')
        
        # Add source files from dependent inline libraries
        inline_libs = ['strbuf', 'strview', 'mem-pool', 'datetime', 'string', 'num_stack', 'url']
        for dep in dependencies:
            if dep in inline_libs:
                # Find the actual library definition to get its sources
                for config_lib in self.config.get('libraries', []):
                    if config_lib.get('name') == dep and 'sources' in config_lib:
                        for source in config_lib['sources']:
                            self.premake_content.append(f'        "{source}",')
        
        self.premake_content.extend([
            '    }',
            '    ',
            '    includedirs {',
            '        "lib/mem-pool/include",',
            '    }',
            '    '
        ])
        
        # Add library dependencies for meta-libraries
        if dependencies:
            external_deps = [dep for dep in dependencies if dep not in inline_libs]
            if external_deps:
                self.premake_content.extend([
                    '    libdirs {',
                    '        "/opt/homebrew/lib",',
                    '        "/opt/homebrew/Cellar/criterion/2.4.2_2/lib",',
                    '        "/usr/local/lib",',
                    '    }',
                    '    ',
                    '    links {',
                ])
                for dep in external_deps:
                    if dep == 'criterion':
                        self.premake_content.append('        "criterion",')
                    else:
                        self.premake_content.append(f'        "{dep}",')
                self.premake_content.extend([
                    '    }',
                    '    '
                ])
        
        self.premake_content.extend([
            '    buildoptions {',
            '        "-fms-extensions",',
            '        "-fcolor-diagnostics",',
            '        "-pedantic"',
            '    }',
            '    ',
            ''
        ])
    
    def generate_test_projects(self) -> None:
        """Generate test executable projects from test suites"""
        test_config = self.config.get('test', {})
        test_suites = test_config.get('test_suites', [])
        
        for suite in test_suites:
            self._generate_test_suite(suite)
    
    def _generate_test_suite(self, suite: Dict[str, Any]) -> None:
        """Generate test projects for a specific test suite"""
        suite_name = suite.get('suite', '')
        special_flags = suite.get('special_flags', '')
        cpp_flags = suite.get('cpp_flags', '')
        
        # Handle both old and new configuration formats
        if 'tests' in suite:
            # New format: tests array with individual test objects
            tests = suite.get('tests', [])
            for test in tests:
                source = test.get('source', '')
                binary_name = test.get('binary', '').replace('.exe', '')
                dependencies = test.get('dependencies', [])
                libraries = test.get('libraries', [])
                defines = test.get('defines', [])
                test_name_override = test.get('name', '')
                
                if not source or not binary_name:
                    continue
                    
                # Avoid subdirectory structure by flattening test names
                test_name = binary_name.replace('/', '_').replace('test_', '') 
                test_name = f"test_{test_name}"
                
                # Determine correct file path - use relative paths from project root
                if source.startswith("test/"):
                    test_file_path = source
                else:
                    test_file_path = f"test/{source}"
                
                # Ensure path exists before adding to project
                actual_path = source if source.startswith("test/") else f"test/{source}"
                full_path = os.path.join(os.getcwd(), actual_path)
                if not os.path.exists(full_path):
                    print(f"Warning: Test file not found: {actual_path}")
                    continue
                    
                self._generate_single_test(test_name, test_file_path, dependencies, special_flags, cpp_flags, libraries, defines)
        else:
            # Old format: parallel arrays (for backward compatibility)
            sources = suite.get('sources', [])
            binaries = suite.get('binaries', [])
            library_deps = suite.get('library_dependencies', [])
            
            # Generate individual test executables
            for i, source in enumerate(sources):
                if i < len(binaries):
                    binary_name = binaries[i].replace('.exe', '')
                    dependencies = library_deps[i] if i < len(library_deps) else []
                    
                    # Avoid subdirectory structure by flattening test names
                    test_name = binary_name.replace('/', '_').replace('test_', '') 
                    test_name = f"test_{test_name}"
                    
                    # Determine correct file path - use relative paths from project root
                    if source.startswith("test/"):
                        test_file_path = source
                    else:
                        test_file_path = f"test/{source}"
                    
                    # Ensure path exists before adding to project
                    actual_path = source if source.startswith("test/") else f"test/{source}"
                    full_path = os.path.join(os.getcwd(), actual_path)
                    if not os.path.exists(full_path):
                        print(f"Warning: Test file not found: {actual_path}")
    
    def _generate_single_test(self, test_name: str, test_file_path: str, dependencies: List[str], 
                             special_flags: str, cpp_flags: str, libraries: List[str] = None, defines: List[str] = None) -> None:
        """Generate a single test project"""
        if libraries is None:
            libraries = []
        if defines is None:
            defines = []
            
        source = test_file_path
        language = "C" if source.endswith('.c') else "C++"
        
        self.premake_content.extend([
            f'project "{test_name}"',
            '    kind "ConsoleApp"',
            f'    language "{language}"',
            '    targetdir "test"',
            '    objdir "build/obj/%{prj.name}"',
            '    targetextension ".exe"',
            '    ',
            '    files {',
            f'        "{test_file_path}",',
            '    }',
            '    '
        ])
        
        # Add include directories using parsed library definitions
        self.premake_content.extend([
            '    includedirs {',
            '        "lib/mem-pool/include",',
        ])
        
        # Add external library include paths from parsed definitions
        for lib_name, lib_info in self.external_libraries.items():
            if lib_info['include']:
                self.premake_content.append(f'        "{lib_info["include"]}",')
        
        # Add system include paths
        self.premake_content.extend([
            '        "/usr/local/include",',
            '        "/opt/homebrew/include",',
            '        "/opt/homebrew/Cellar/criterion/2.4.2_2/include",',
            '    }',
            '    '
        ])
        
        # Add defines if specified
        if defines:
            self.premake_content.append('    defines {')
            for define in defines:
                self.premake_content.append(f'        "{define}",')
            self.premake_content.extend([
                '    }',
                '    '
            ])
        
        # Add library paths
        self.premake_content.extend([
            '    libdirs {',
            '        "/opt/homebrew/lib",',
            '        "/opt/homebrew/Cellar/criterion/2.4.2_2/lib",',
            '        "/usr/local/lib",',
            '        "build/lib",',
            '    }',
            '    '
        ])
        
        # Add library dependencies
        if dependencies:
            self.premake_content.append('    links {')
            for dep in dependencies:
                if dep == 'criterion':
                    self.premake_content.append('        "criterion",')
                elif dep == 'lambda-lib':
                    # Lambda-lib contains all the core libraries
                    self.premake_content.append('        "lambda-lib",')
                elif dep in ['lambda-runtime-full', 'lambda-input-full']:
                    # Special handling for MIR, Lambda, Math, and Markup tests
                    if ('mir' in test_name.lower() or 'lambda' in test_name.lower() or 'math' in test_name.lower() or 'markup' in test_name.lower()) and dep == 'lambda-runtime-full':
                        if language == "C":
                            # C tests need both C and C++ runtime libraries
                            self.premake_content.append('        "lambda-runtime-full-cpp",')
                            self.premake_content.append('        "lambda-runtime-full-c",')
                            self.premake_content.append('        "lambda-input-full-cpp",')
                            self.premake_content.append('        "lambda-input-full-c",')
                        else:
                            # C++ tests need both C and C++ versions
                            self.premake_content.append('        "lambda-runtime-full-cpp",')
                            self.premake_content.append('        "lambda-runtime-full-c",')
                            self.premake_content.append('        "lambda-input-full-cpp",')
                            self.premake_content.append('        "lambda-input-full-c",')
                    else:
                        # Regular tests: For C++ tests, link both C++ and C versions (C++ depends on C for core utilities)
                        if language == "C++":
                            self.premake_content.append(f'        "{dep}-cpp",')
                            self.premake_content.append(f'        "{dep}-c",')
                        else:
                            self.premake_content.append(f'        "{dep}-c",')
                    
                    # Add lambda-lib which now contains all core dependencies
                    self.premake_content.append('        "lambda-lib",')
            
            # Always add criterion to test executables since static libraries don't propagate criterion symbols
            if 'criterion' not in dependencies:
                self.premake_content.append('        "criterion",')
            
            # Close the links block
            self.premake_content.extend([
                '    }',
                '    '
            ])
            
            # Add external library linkoptions for test-specific libraries
            if libraries:
                external_static_libs = []
                for lib_name in libraries:
                    if lib_name in self.external_libraries:
                        lib_info = self.external_libraries[lib_name]
                        if lib_info.get('link') == 'static':
                            lib_path = lib_info['lib']
                            if not lib_path.startswith('/'):
                                lib_path = f"../../{lib_path}"
                            external_static_libs.append(lib_path)
                
                if external_static_libs:
                    self.premake_content.append('    linkoptions {')
                    for lib_path in external_static_libs:
                        self.premake_content.append(f'        "{lib_path}",')
                    self.premake_content.extend([
                        '    }',
                        '    '
                    ])
        else:
            # No library dependencies specified, but still need criterion for tests
            self.premake_content.extend([
                '    links {',
                '        "criterion",',
                '    }',
                '    '
            ])
        
        # Add external library paths for linking when lambda-runtime-full or lambda-input-full are used
        if any(dep in ['lambda-runtime-full', 'lambda-input-full'] or dep.startswith('lambda-runtime-full-') or dep.startswith('lambda-input-full-') for dep in dependencies):
            self.premake_content.extend([
                '    linkoptions {',
            ])
            
            # Add tree-sitter libraries using parsed definitions
            for lib_name in ['tree-sitter-lambda', 'tree-sitter']:
                if lib_name in self.external_libraries:
                    lib_path = self.external_libraries[lib_name]['lib']
                    # Convert to relative path from build directory
                    if not lib_path.startswith('/'):
                        lib_path = f"../../{lib_path}"
                    self.premake_content.append(f'        "{lib_path}",')
            
            # Add other external libraries
            # If lambda-input-full is a dependency, we need to include curl/ssl/crypto/nghttp2 for proper linking
            # since static libraries don't propagate their dependencies in Premake
            for lib_name in ['mpdec', 'utf8proc', 'mir', 'curl', 'nghttp2', 'ssl', 'crypto', 'libedit']:
                if lib_name in self.external_libraries:
                    lib_path = self.external_libraries[lib_name]['lib']
                    # Convert to relative path from build directory
                    if not lib_path.startswith('/'):
                        lib_path = f"../../{lib_path}"
                    self.premake_content.append(f'        "{lib_path}",')
            
            self.premake_content.extend([
                '    }',
                '    ',
                '    -- Add dynamic libraries',
                '    links {'
            ])
            
            # Add dynamic libraries (not frameworks)
            for lib_name in ['z']:
                if lib_name in self.external_libraries and self.external_libraries[lib_name].get('link') == 'dynamic':
                    lib_flag = self.external_libraries[lib_name]['lib']
                    if lib_flag.startswith('-l'):
                        lib_flag = lib_flag[2:]  # Remove -l prefix
                    self.premake_content.append(f'        "{lib_flag}",')
            
            # Add system libraries that libedit depends on
            self.premake_content.append('        "ncurses",')
            
            self.premake_content.extend([
                '    }',
                '    ',
                '    -- Add macOS frameworks',
                '    linkoptions {'
            ])
            
            # Add macOS frameworks using linkoptions
            for lib_name in ['CoreFoundation', 'CoreServices', 'SystemConfiguration']:
                if lib_name in self.external_libraries and self.external_libraries[lib_name].get('link') == 'dynamic':
                    lib_flag = self.external_libraries[lib_name]['lib']
                    if lib_flag.startswith('-framework '):
                        self.premake_content.append(f'        "{lib_flag}",')
            
            self.premake_content.extend([
                '    }',
                '    '
            ])
        
        # Add build options based on source file type
        is_cpp_test = source.endswith('.cpp')
        build_opts = ['-fms-extensions', '-fcolor-diagnostics', '-pedantic']
        
        if is_cpp_test:
            if cpp_flags:
                build_opts.append(cpp_flags)
            if special_flags and '-std=c++17' in special_flags:
                build_opts.append('-std=c++17')
            if special_flags and '-lstdc++' in special_flags:
                self.premake_content.extend([
                    '    links { "stdc++" }',
                    '    '
                ])
        else:
            # For C files, use C99 standard
            build_opts.append('-std=c99')
        
        self.premake_content.extend([
            '    buildoptions {',
        ])
        for opt in build_opts:
            self.premake_content.append(f'        "{opt}",')
        self.premake_content.extend([
            '    }',
            '    ',
            ''
        ])
    
    def generate_main_program(self) -> None:
        """Generate the main Lambda program executable"""
        output = self.config.get('output', 'lambda.exe')
        source_files = self.config.get('source_files', [])
        source_dirs = self.config.get('source_dirs', [])
        dependencies = []
        
        # Extract main program dependencies from libraries
        for lib in self.config.get('libraries', []):
            lib_name = lib.get('name', '')
            if lib_name not in ['criterion']:  # Exclude test-only libraries
                dependencies.append(lib_name)
        
        # Add dev_libraries for main program
        for lib in self.config.get('dev_libraries', []):
            lib_name = lib.get('name', '')
            dependencies.append(lib_name)
        
        # Remove .exe extension for project name
        project_name = output.replace('.exe', '')
        
        # Add files from source directories explicitly
        all_source_files = source_files[:]
        for source_dir in source_dirs:
            import glob
            c_pattern = f"{source_dir}/**/*.c"
            cpp_pattern = f"{source_dir}/**/*.cpp"
            
            # Find all C files
            c_files = glob.glob(c_pattern, recursive=True)
            all_source_files.extend(c_files)
            
            # Find all C++ files  
            cpp_files = glob.glob(cpp_pattern, recursive=True)
            all_source_files.extend(cpp_files)
        
        self.premake_content.extend([
            f'project "{project_name}"',
            '    kind "ConsoleApp"',
            '    language "C++"',  # Use C++ as primary language since we have mixed sources
            '    targetdir "."',
            '    objdir "build/obj/%{prj.name}"',
            '    targetname "lambda"',
            '    targetextension ".exe"',
            '    ',
            '    files {',
        ])
        
        # Add all source files explicitly
        for source in all_source_files:
            self.premake_content.append(f'        "{source}",')
        
        self.premake_content.extend([
            '    }',
            '    ',
            '    includedirs {',
            '        ".",',
            '        "lambda/tree-sitter/lib/include",',
            '        "lambda/tree-sitter-lambda/bindings/c",',
            '        "lib/mem-pool/include",',
        ])
        
        # Add external library include paths
        for lib_name, lib_info in self.external_libraries.items():
            if lib_info['include'] and lib_name != 'criterion':
                self.premake_content.append(f'        "{lib_info["include"]}",')
        
        self.premake_content.extend([
            '        "/usr/local/include",',
            '        "/opt/homebrew/include",',
            '    }',
            '    ',
            '    libdirs {',
            '        "/opt/homebrew/lib",',
            '        "/usr/local/lib",',
            '        "build/lib",',
            '    }',
            '    '
        ])
        
        # Add static library linkoptions
        static_libs = []
        frameworks = []
        dynamic_libs = []
        
        for dep in dependencies:
            if dep in self.external_libraries:
                lib_info = self.external_libraries[dep]
                lib_path = lib_info['lib']
                
                if lib_info.get('link') == 'dynamic':
                    if lib_path.startswith('-framework '):
                        frameworks.append(lib_path)
                    elif lib_path.startswith('-l'):
                        dynamic_libs.append(lib_path.replace('-l', ''))
                    else:
                        dynamic_libs.append(lib_path)
                else:
                    # Static library
                    if not lib_path.startswith('/'):
                        lib_path = f"../../{lib_path}"
                    static_libs.append(lib_path)
        
        # Add static libraries to linkoptions
        if static_libs:
            self.premake_content.append('    linkoptions {')
            for lib_path in static_libs:
                self.premake_content.append(f'        "{lib_path}",')
            self.premake_content.extend([
                '    }',
                '    '
            ])
        
        # Add dynamic libraries and frameworks
        if frameworks or dynamic_libs:
            self.premake_content.append('    links {')
            for lib in dynamic_libs:
                self.premake_content.append(f'        "{lib}",')
            self.premake_content.extend([
                '    }',
                '    '
            ])
            
            if frameworks:
                self.premake_content.append('    linkoptions {')
                for framework in frameworks:
                    self.premake_content.append(f'        "{framework}",')
                self.premake_content.extend([
                    '    }',
                    '    '
                ])
        
        # Add build options with separate handling for C and C++ files
        self.premake_content.extend([
            '    buildoptions {',
            '        "-fms-extensions",',
            '        "-fcolor-diagnostics",',
            '        "-pedantic"',
            '    }',
            '    ',
            '    -- C++ specific options',
            '    filter "files:**.cpp"',
            '        buildoptions { "-std=c++17" }',
            '    ',
            '    -- C specific options',
            '    filter "files:**.c"',
            '        buildoptions { "-std=c99" }',
            '    ',
            '    filter {}',
            '    ',
            '    defines {',
            '        "_GNU_SOURCE"',
            '    }',
            '    ',
            ''
        ])

    def generate_premake_file(self, output_path: str = "premake5.lua") -> None:
        """Generate the complete premake5.lua file"""
        self.premake_content = []
        
        # Add header comment
        self.premake_content.extend([
            '-- Generated by utils/generate_premake.py',
            '-- Lambda Build System Premake5 Configuration',
            '-- DO NOT EDIT MANUALLY - Regenerate using: python3 utils/generate_premake.py',
            '',
        ])
        
        # Generate all sections
        self.generate_workspace()
        self.generate_library_projects()
        self.generate_complex_libraries()
        self.generate_main_program()
        self.generate_test_projects()
        
        # Write to file
        try:
            with open(output_path, 'w') as f:
                f.write('\n'.join(self.premake_content))
            print(f"Generated {output_path} successfully")
        except IOError as e:
            print(f"Error writing {output_path}: {e}")
            sys.exit(1)
    
    def validate_config(self) -> bool:
        """Validate the JSON configuration"""
        required_sections = ['libraries', 'test']
        for section in required_sections:
            if section not in self.config:
                print(f"Error: Missing required section '{section}' in configuration")
                return False
        
        test_config = self.config['test']
        if 'test_suites' not in test_config:
            print("Error: Missing 'test_suites' in test configuration")
            return False
        
        return True

def main():
    """Main entry point"""
    config_file = "build_lambda_config.json"
    output_file = "premake5.lua"
    
    # Parse command line arguments
    if len(sys.argv) > 1:
        config_file = sys.argv[1]
    if len(sys.argv) > 2:
        output_file = sys.argv[2]
    
    # Generate Premake5 configuration
    generator = PremakeGenerator(config_file)
    generator.parse_config()
    
    if not generator.validate_config():
        sys.exit(1)
    
    generator.generate_premake_file(output_file)
    print(f"Premake5 migration completed successfully!")
    print(f"Next steps:")
    print(f"  1. Run: premake5 gmake2")
    print(f"  2. Run: make -C build/premake config=debug")

if __name__ == "__main__":
    main()
