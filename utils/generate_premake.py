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
import platform
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Any, Optional

class PremakeGenerator:
    def __init__(self, config_path: str = "build_lambda_config.json", explicit_platform: str = None):
        with open(config_path, 'r', encoding='utf-8') as f:
            self.config = json.load(f)
        self.premake_content = []

        # Add platform detection for use throughout the generator
        import platform
        current_platform = platform.system()

        # If explicit platform is provided, use it to override platform detection
        if explicit_platform:
            if explicit_platform in ['mac', 'macos', 'darwin']:
                self.use_linux_config = False
                self.use_macos_config = True
                self.use_windows_config = False
            elif explicit_platform in ['linux', 'lin']:
                self.use_linux_config = True
                self.use_macos_config = False
                self.use_windows_config = False
            elif explicit_platform in ['windows', 'win']:
                self.use_linux_config = False
                self.use_macos_config = False
                self.use_windows_config = True
            else:
                raise ValueError(f"Unknown platform '{explicit_platform}'. Use 'mac', 'linux', or 'windows'")
        else:
            # Use auto-detection
            self.use_linux_config = (current_platform == 'Linux' or
                                     self.config.get('platform') == 'Linux_x64' or
                                     self.config.get('platform') == 'Linux')
            self.use_macos_config = (current_platform == 'Darwin' or
                                    self.config.get('platform') == 'macOS' or
                                    self.config.get('platform') == 'Darwin')
            self.use_windows_config = (current_platform == 'Windows' or
                                      current_platform.startswith('MINGW') or
                                      current_platform.startswith('MSYS') or
                                      current_platform.startswith('CYGWIN') or
                                      'MSYS_NT' in current_platform or
                                      'MINGW' in current_platform or
                                      self.config.get('platform') == 'Windows')

        self.external_libraries = self._parse_external_libraries()

    def _parse_external_libraries(self) -> Dict[str, Dict[str, str]]:
        """Parse external library definitions from JSON config

        Logic:
        1. Libraries can be defined at global level
        2. Platform-specific libraries override global settings
        3. Platform can set link:'none' to exclude a globally-defined library
        """
        libraries = {}

        # Step 1: Parse global libraries first
        for lib in self.config.get('libraries', []):
            # Include all libraries that have a 'name' field
            if 'name' in lib:
                libraries[lib['name']] = {
                    'include': lib.get('include', ''),
                    'lib': lib.get('lib', ''),
                    'link': lib.get('link', 'static')
                }

        # Parse global dev_libraries (development/test-only libraries)
        for lib in self.config.get('dev_libraries', []):
            if 'name' in lib:
                libraries[lib['name']] = {
                    'include': lib.get('include', ''),
                    'lib': lib.get('lib', ''),
                    'link': lib.get('link', 'static')
                }

        # Step 2: Apply platform-specific overrides
        platforms_config = self.config.get('platforms', {})

        # Override with Linux-specific libraries if on Linux
        if self.use_linux_config:
            linux_config = platforms_config.get('linux', {})

            # Process Linux-specific libraries
            for lib in linux_config.get('libraries', []):
                if 'name' in lib:
                    if lib.get('link') == 'none':
                        # Remove library if it was globally defined
                        if lib['name'] in libraries:
                            del libraries[lib['name']]
                    else:
                        # Override or add library
                        libraries[lib['name']] = {
                            'include': lib.get('include', ''),
                            'lib': lib.get('lib', ''),
                            'link': lib.get('link', 'static')
                        }

            # Process Linux-specific dev_libraries
            for lib in linux_config.get('dev_libraries', []):
                if 'name' in lib:
                    if lib.get('link') == 'none':
                        # Remove library if it was globally defined
                        if lib['name'] in libraries:
                            del libraries[lib['name']]
                    else:
                        # Override or add library
                        libraries[lib['name']] = {
                            'include': lib.get('include', ''),
                            'lib': lib.get('lib', ''),
                            'link': lib.get('link', 'static')
                        }

        # Override with macOS-specific libraries if on macOS
        if self.use_macos_config:
            macos_config = platforms_config.get('macos', {})

            # Process macOS-specific libraries
            for lib in macos_config.get('libraries', []):
                if 'name' in lib:
                    if lib.get('link') == 'none':
                        # Remove library if it was globally defined
                        if lib['name'] in libraries:
                            del libraries[lib['name']]
                    else:
                        # Override or add library
                        libraries[lib['name']] = {
                            'include': lib.get('include', ''),
                            'lib': lib.get('lib', ''),
                            'link': lib.get('link', 'dynamic')  # Default to dynamic on macOS
                        }

            # Process macOS-specific dev_libraries
            for lib in macos_config.get('dev_libraries', []):
                if 'name' in lib:
                    if lib.get('link') == 'none':
                        # Remove library if it was globally defined
                        if lib['name'] in libraries:
                            del libraries[lib['name']]
                    else:
                        # Override or add library
                        libraries[lib['name']] = {
                            'include': lib.get('include', ''),
                            'lib': lib.get('lib', ''),
                            'link': lib.get('link', 'dynamic')  # Default to dynamic on macOS
                        }

        # Override with Windows-specific libraries if on Windows
        if self.use_windows_config:
            windows_config = platforms_config.get('windows', {})

            # Process Windows-specific libraries
            for lib in windows_config.get('libraries', []):
                if 'name' in lib:
                    if lib.get('link') == 'none':
                        # Remove library if it was globally defined
                        if lib['name'] in libraries:
                            del libraries[lib['name']]
                    else:
                        # Override or add library
                        libraries[lib['name']] = {
                            'include': lib.get('include', ''),
                            'lib': lib.get('lib', ''),
                            'link': lib.get('link', 'static')
                        }

            # Process Windows-specific dev_libraries
            for lib in windows_config.get('dev_libraries', []):
                if 'name' in lib:
                    if lib.get('link') == 'none':
                        # Remove library if it was globally defined
                        if lib['name'] in libraries:
                            del libraries[lib['name']]
                    else:
                        # Override or add library
                        libraries[lib['name']] = {
                            'include': lib.get('include', ''),
                            'lib': lib.get('lib', ''),
                            'link': lib.get('link', 'static')
                        }

        return libraries

    def _is_lambda_input_full_dependent_test(self, target_name: str) -> bool:
        """Check if a test target depends on lambda-input-full libraries"""
        # Try to match by binary name (with or without .exe and with or without test/ prefix)
        target_binary = target_name + '.exe' if not target_name.endswith('.exe') else target_name
        target_binary_with_path = 'test/' + target_binary

        # Check test_suites - first check if we have 'test' section
        test_config = self.config.get('test', {})
        if 'test_suites' in test_config:
            for suite in test_config['test_suites']:
                if 'tests' in suite:
                    for test in suite['tests']:
                        binary = test.get('binary', '')
                        name = test.get('name', '')
                        dependencies = test.get('dependencies', [])

                        # Check multiple variations: exact binary match, binary with path, or name match
                        if (binary == target_binary or
                            binary == target_binary_with_path or
                            name == target_name):
                            result = any(dep in ['lambda-runtime-full', 'lambda-input-full'] or
                                      dep.startswith('lambda-runtime-full-') or
                                      dep.startswith('lambda-input-full-')
                                      for dep in dependencies)
                            return result

        # Also check top-level test_suites (if any)
        if 'test_suites' in self.config:
            for suite in self.config['test_suites']:
                if 'tests' in suite:
                    for test in suite['tests']:
                        binary = test.get('binary', '')
                        name = test.get('name', '')
                        # Check multiple variations: exact binary match, binary with path, or name match
                        if (binary == target_binary or
                            binary == target_binary_with_path or
                            name == target_name):
                            dependencies = test.get('dependencies', [])
                            result = any(dep in ['lambda-runtime-full', 'lambda-input-full'] or
                                      dep.startswith('lambda-runtime-full-') or
                                      dep.startswith('lambda-input-full-')
                                      for dep in dependencies)
                            return result

        return False

    def _get_compiler_info(self) -> tuple[str, str]:
        """Get compiler and toolset information based on platform configuration"""
        # Get compiler from config - check for platform-specific config first
        platforms_config = self.config.get('platforms', {})

        # Use platform-specific compiler if available, otherwise use global
        if self.use_windows_config:
            windows_config = platforms_config.get('windows', {})
            compiler = windows_config.get('compiler', self.config.get('compiler', 'clang'))
        else:
            linux_config = platforms_config.get('linux', {})
            if linux_config:
                compiler = linux_config.get('compiler', self.config.get('compiler', 'clang'))
            else:
                compiler = self.config.get('compiler', 'clang')

        # Extract compiler name from path
        compiler_name = os.path.basename(compiler)
        if 'gcc' in compiler_name:
            base_compiler = 'gcc'
        elif 'g++' in compiler_name:
            base_compiler = 'g++'
        elif 'clang' in compiler_name:
            base_compiler = 'clang'
        else:
            base_compiler = 'clang'  # default fallback

        # Map compiler to Premake toolset
        toolset_map = {
            'clang': 'clang',
            'gcc': 'gcc',
            'g++': 'gcc'
        }
        toolset = toolset_map.get(base_compiler, 'clang')

        return base_compiler, toolset

    def _get_build_options(self, base_compiler: str) -> List[str]:
        """Get compiler-specific build options"""
        build_opts = ['-pedantic']

        # Add compiler-specific flags
        if base_compiler in ['gcc', 'g++']:
            build_opts.extend(['-fdiagnostics-color=auto'])
            # gcc doesn't need -fms-extensions and doesn't support -fcolor-diagnostics
        elif base_compiler == 'clang':
            build_opts.extend(['-fms-extensions', '-fcolor-diagnostics'])

        # Add global flags from configuration
        global_flags = self.config.get('flags', [])
        for flag in global_flags:
            if flag.startswith('D'):  # Define flags like DRPMALLOC_FIRST_CLASS_HEAPS=1
                build_opts.append(f'-{flag}')
            elif flag not in ['pedantic', 'fdiagnostics-color=auto', 'fms-extensions']:  # Avoid duplicates
                build_opts.append(f'-{flag}')

        # Add platform-specific flags
        platforms_config = self.config.get('platforms', {})
        if self.use_windows_config:
            windows_config = platforms_config.get('windows', {})
            platform_flags = windows_config.get('flags', [])
            for flag in platform_flags:
                opt = f'-{flag}' if not flag.startswith('-') else flag
                if opt not in build_opts:
                    build_opts.append(opt)
        elif self.use_linux_config:
            linux_config = platforms_config.get('linux', {})
            platform_flags = linux_config.get('flags', [])
            for flag in platform_flags:
                opt = f'-{flag}' if not flag.startswith('-') else flag
                if opt not in build_opts:
                    build_opts.append(opt)
        elif self.use_macos_config:
            macos_config = platforms_config.get('macos', {})
            platform_flags = macos_config.get('flags', [])
            for flag in platform_flags:
                opt = f'-{flag}' if not flag.startswith('-') else flag
                if opt not in build_opts:
                    build_opts.append(opt)

        return build_opts

    def _get_consolidated_includes(self) -> List[str]:
        """Get consolidated include directories from global and platform-specific configurations"""
        includes = []

        # Add global includes first
        global_includes = self.config.get('includes', [])
        includes.extend(global_includes)

        # Add platform-specific includes
        platforms_config = self.config.get('platforms', {})

        if self.use_linux_config:
            linux_config = platforms_config.get('linux', {})
            linux_includes = linux_config.get('includes', [])
            includes.extend(linux_includes)
        elif self.use_macos_config:
            macos_config = platforms_config.get('macos', {})
            macos_includes = macos_config.get('includes', [])
            includes.extend(macos_includes)
        elif self.use_windows_config:
            windows_config = platforms_config.get('windows', {})
            windows_includes = windows_config.get('includes', [])
            includes.extend(windows_includes)

        # Remove duplicates while preserving order
        seen = set()
        unique_includes = []
        for include in includes:
            if include and include not in seen:
                unique_includes.append(include)
                seen.add(include)

        return unique_includes

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

    def _get_platform_info(self) -> tuple[str, List[str]]:
        """Get platform and architecture information"""
        # For simplicity, don't specify architecture to avoid -m64 issues on ARM64
        platforms = ['native']

        return 'native', platforms

    def _get_language_info(self, target: Dict[str, Any] = None) -> tuple[str, str, bool]:
        """Get language information from target or global configuration

        Returns: (language_type, standard_version, needs_cpp_stdlib)
        Examples: ("C++", "c++17", True), ("C", "c17", False)
        """
        # Get language from target first, then global config
        lang = None
        if target:
            lang = target.get('lang')
        if not lang:
            lang = self.config.get('lang', 'c++17')

        # Parse language and standard
        if lang.startswith('c++'):
            return "C++", lang, True
        elif lang.startswith('c') and lang[1:].isdigit():
            return "C", lang, False
        else:
            # Fallback: detect from source files if target provided
            if target:
                source_files = target.get('source_files', [])
                has_cpp = any(f.endswith('.cpp') for f in source_files)
                if has_cpp:
                    return "C++", "c++17", True
                else:
                    return "C", "c17", False
            else:
                return "C++", "c++17", True

    def _get_cpp_standard(self, target: Dict[str, Any] = None) -> str:
        """Get the C++ standard version from configuration

        Checks multiple sources in order of preference:
        1. Target-specific lang field
        2. Global lang field
        3. Global cpp_flags for -std=c++XX
        4. Platform-specific cpp_flags
        5. Default to c++17
        """
        # Use the new language info function
        _, standard, _ = self._get_language_info(target)
        return standard

    def generate_workspace(self) -> None:
        """Generate the main workspace configuration"""
        print("DEBUG: Generating workspace configuration...")

        # Get workspace name from config or default
        workspace_name = self.config.get('workspace_name', 'Lambda')
        output = self.config.get('output', 'lambda.exe')
        startup_project = output.replace('.exe', '')

        print(f"DEBUG: workspace_name={workspace_name}, output={output}, startup_project={startup_project}")

        # Get compiler information
        base_compiler, toolset = self._get_compiler_info()
        print(f"DEBUG: base_compiler={base_compiler}, toolset={toolset}")

        # Get platform and architecture information
        arch, platforms = self._get_platform_info()
        print(f"DEBUG: arch={arch}, platforms={platforms}")

        platform_str = ', '.join([f'"{p}"' for p in platforms])

        # Set location based on platform
        platform_config = self.config.get('platform', 'macOS')
        if platform_config == 'Linux_x64':
            location = 'build_linux/test'
        else:
            location = 'build/premake'
        print(f"DEBUG: platform_config={platform_config}, location={location}")

        self.premake_content.extend([
            f'workspace "{workspace_name}"',
            '    configurations { "Debug", "Release" }',
            f'    platforms {{ {platform_str} }}',
            f'    location "{location}"',
            f'    startproject "{startup_project}"',
            f'    toolset "{toolset}"',
            '    ',
            '    -- Global settings',
            '    cppdialect "C++17"',
            '    cdialect "C99"',
            '    warnings "Extra"',
            '    ',
        ])

        self.premake_content.extend([
            '    filter "configurations:Debug"',
            '        defines { "DEBUG" }',
            '        symbols "On"',
            '        optimize "Off"',
        ])

        # Add Windows-specific linker flags to debug configuration
        if self.use_windows_config:
            platforms_config = self.config.get('platforms', {})
            windows_config = platforms_config.get('windows', {})
            linker_flags = windows_config.get('linker_flags', [])
            print(f"DEBUG: Adding Windows linker flags to Debug configuration: {linker_flags}")

            if linker_flags:
                self.premake_content.append('        linkoptions {')
                for flag in linker_flags:
                    if flag.startswith('l'):
                        # Library flags start with 'l' (like lwinmm)
                        self.premake_content.append(f'            "-{flag}",')
                        print(f"DEBUG: Added Windows library flag to Debug: -{flag}")
                    elif flag.startswith('Wl,'):
                        # Linker options start with 'Wl,'
                        self.premake_content.append(f'            "-{flag}",')
                        print(f"DEBUG: Added Windows linker option to Debug: -{flag}")
                    else:
                        # Other flags like 'static', 'static-libgcc'
                        self.premake_content.append(f'            "-{flag}",')
                        print(f"DEBUG: Added Windows other flag to Debug: -{flag}")
                self.premake_content.extend([
                    '        }',
                ])
                print("DEBUG: Added Windows linker flags to Debug configuration")

        self.premake_content.extend([
            '    ',
        ])

        print("DEBUG: Added Debug configuration filter")

        # Check if sanitizer should be disabled for linux platform
        platforms_config = self.config.get('platforms', {})
        linux_config = platforms_config.get('linux', {})
        disable_sanitizer = linux_config.get('disable_sanitizer', False)

        print(f"DEBUG: platforms_config keys: {list(platforms_config.keys())}")
        print(f"DEBUG: linux_config: {linux_config}")
        print(f"DEBUG: disable_sanitizer: {disable_sanitizer}")

        # Note: AddressSanitizer will be applied individually to test projects only
        # This avoids applying it to the main lambda.exe executable
        if not disable_sanitizer:
            print("DEBUG: AddressSanitizer will be applied to test projects only")
        else:
            self.premake_content.extend([
                '    -- AddressSanitizer disabled for Linux platform',
                '    ',
            ])
            print("DEBUG: AddressSanitizer disabled")

        # Release configuration with size optimizations
        self.premake_content.extend([
            '    filter "configurations:Release"',
            '        defines { "NDEBUG" }',
            '        symbols "Off"',
            '        optimize "On"',
            '        -- Dead code elimination and ThinLTO',
            '        buildoptions {',
            '            "-flto=thin",',
            '            "-ffunction-sections",',
            '            "-fdata-sections",',
            '            "-fvisibility=hidden",',
            '            "-fvisibility-inlines-hidden",',
            '        }',
        ])

        # Platform-specific linker flags for dead code stripping
        if self.use_macos_config:
            self.premake_content.extend([
                '        -- macOS: strip dead code and symbols with ThinLTO',
                '        linkoptions {',
                '            "-flto=thin",',
                '            "-Wl,-dead_strip",',
                '            "-Wl,-x",  -- Strip local symbols',
                '        }',
            ])
        elif self.use_linux_config:
            self.premake_content.extend([
                '        -- Linux: strip dead code and symbols with ThinLTO',
                '        linkoptions {',
                '            "-flto=thin",',
                '            "-Wl,--gc-sections",',
                '            "-Wl,--strip-all",',
                '        }',
            ])
        elif self.use_windows_config:
            self.premake_content.extend([
                '        -- Windows: strip dead code with ThinLTO',
                '        linkoptions {',
                '            "-flto=thin",',
                '            "-Wl,--gc-sections",',
                '            "-s",  -- Strip symbols',
                '        }',
            ])

        self.premake_content.extend([
            '    ',
        ])

        # Note: Windows linker flags are now added to Debug configuration above, not globally
        if platform_config == 'Linux_x64' or 'linux' in output.lower() or base_compiler in ['gcc', 'g++']:
            self.premake_content.extend([
                '    -- Native Linux build settings',
                f'    toolset "{toolset}"',
                '    defines { "LINUX", "_GNU_SOURCE", "NATIVE_LINUX_BUILD" }',
                '    ',
            ])

            # Add library search paths for Linux dependencies
            lib_dirs = self.config.get('lib_dirs', self.config.get('library_dirs', []))
            if lib_dirs:
                lib_dirs_str = ', '.join([f'"{d}"' for d in lib_dirs])
                self.premake_content.append(f'        libdirs {{ {lib_dirs_str} }}')

        self.premake_content.extend([
            '    ',
            '    filter {}',
            ''
        ])

    def generate_library_projects(self) -> None:
        """Generate static library projects from JSON config"""
        # Handle new lib_project format (for cross-platform builds)
        lib_project = self.config.get('lib_project', {})
        if lib_project:
            self._generate_lib_project(lib_project)

        # Handle old libraries format (backward compatibility)
        libraries = self.config.get('libraries', [])

        for lib in libraries:
            # Handle both string and object formats
            if isinstance(lib, str):
                # String format: just library name, skip processing
                continue
            elif isinstance(lib, dict):
                lib_name = lib.get('name', '')
                link_type = lib.get('link', 'static')

                # Skip external libraries
                if link_type in ['dynamic', 'static'] and 'sources' not in lib:
                    continue

    def _generate_lib_project(self, lib_project: Dict[str, Any]) -> None:
        """Generate a static library project from lib_project configuration"""
        name = lib_project.get('name', 'lambda-lib')
        kind = lib_project.get('kind', 'StaticLib')
        language = lib_project.get('language', 'C')
        target_dir = lib_project.get('target_dir', 'build/lib')
        files = lib_project.get('files', [])

        self.premake_content.extend([
            '',
            f'project "{name}"',
            f'    kind "{kind}"',
            f'    language "{language}"',
            f'    targetdir "{target_dir}"',
            f'    objdir "build/obj/%{{prj.name}}"',
            '    ',
        ])

        # Add source files
        if files:
            self.premake_content.append('    files {')
            for file in files:
                self.premake_content.append(f'        "{file}",')
            self.premake_content.extend([
                '    }',
                '    '
            ])

        # Add include directories
        consolidated_includes = self._get_consolidated_includes()
        include_dirs = self.config.get('include_dirs', [])

        # Combine legacy include_dirs with new consolidated includes
        all_includes = consolidated_includes + include_dirs

        # Remove duplicates while preserving order
        seen = set()
        unique_includes = []
        for include in all_includes:
            if include and include not in seen:
                unique_includes.append(include)
                seen.add(include)

        if unique_includes:
            self.premake_content.append('    includedirs {')
            for include_dir in unique_includes:
                self.premake_content.append(f'        "{include_dir}",')
            self.premake_content.extend([
                '    }',
                '    '
            ])

        # Add library directories
        lib_dirs = self.config.get('lib_dirs', [])
        if lib_dirs:
            self.premake_content.append('    libdirs {')
            for lib_dir in lib_dirs:
                self.premake_content.append(f'        "{lib_dir}",')
            self.premake_content.extend([
                '    }',
                '    '
            ])

        # Add build options
        cflags = self.config.get('cflags', [])
        cxxflags = self.config.get('cxxflags', [])

        if cflags:
            self.premake_content.extend([
                '    filter "files:**.c"',
                '        buildoptions {'
            ])
            for flag in cflags:
                self.premake_content.append(f'            "{flag}",')
            self.premake_content.extend([
                '        }',
                '    '
            ])

        if cxxflags:
            self.premake_content.extend([
                '    filter "files:**.cpp"',
                '        buildoptions {'
            ])
            for flag in cxxflags:
                self.premake_content.append(f'            "{flag}",')
            self.premake_content.extend([
                '        }',
                '    '
            ])

        # Add defines
        defines = self.config.get('defines', [])
        if defines:
            self.premake_content.extend([
                '    defines {'
            ])
            for define in defines:
                self.premake_content.append(f'        "{define}",')
            self.premake_content.extend([
                '    }',
                '    '
            ])

        # Add platform-specific settings
        platform = self.config.get('platform', '')
        if platform == 'Linux_x64':
            self.premake_content.extend([
                f'    filter "platforms:{platform}"',
                '        system "linux"',
                '        architecture "x64"',
                '        toolset "gcc"',
                '        gccprefix "x86_64-linux-gnu-"',
                '    '
            ])

        self.premake_content.extend([
            '    filter {}',
            '    '
        ])

    def generate_complex_libraries(self) -> None:
        """Generate complex library projects and executable targets"""
        targets = self.config.get('targets', [])

        for lib in targets:
            lib_name = lib.get('name', '')
            # Handle library targets
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

        # If we have both C and C++ files, create a single C++ project that includes all files
        # No need for separate -c project since C++ project already includes C files for proper linking
        if c_files and cpp_files:
            # Create C++ project - include ALL C files for proper linking
            all_files = cpp_files + c_files
            self._create_language_project(lib, all_files, source_patterns, dependencies, "C++", f"{lib_name}-cpp")
            # Create main project that links to the cpp project
            self._create_wrapper_project(lib_name, [f"{lib_name}-cpp"], lib)
        else:
            # Single language project
            language = "C++" if cpp_files or any('*.cpp' in pattern for pattern in source_patterns) else "C"
            self._create_language_project(lib, source_files, source_patterns, dependencies, language, lib_name)

    def _create_language_project(self, lib: Dict[str, Any], source_files: List[str],
                               source_patterns: List[str], dependencies: List[str],
                               language: str, project_name: str) -> None:
        """Create a single-language project"""
        # Get language info from the lib target configuration
        detected_language, standard, needs_cpp_stdlib = self._get_language_info(lib)

        # For split projects (with -c or -cpp suffix), respect the passed language parameter
        # Otherwise, use detected language from configuration
        if project_name.endswith('-c') or project_name.endswith('-cpp'):
            # This is a split project, use the explicitly passed language
            if language == "C":
                detected_language = "C"
                standard = "c17"  # Use C17 standard for C projects
                needs_cpp_stdlib = False
            elif language == "C++":
                # Keep the original C++ settings from the library
                pass
            # Use the explicitly passed language for split projects
            final_language = language
        else:
            # Use detected language if different from passed language for non-split projects
            if detected_language != language:
                final_language = detected_language
            else:
                final_language = language

        # Determine library type based on link attribute
        link_type = lib.get('link', 'static')

        # Force DLL for lambda-input-full on Windows to avoid static library dependency issues
        # if self.use_windows_config and project_name.startswith('lambda-input-full'):
        #     link_type = 'dynamic'

        kind = "SharedLib" if link_type == 'dynamic' else "StaticLib"

        self.premake_content.extend([
            f'project "{project_name}"',
            f'    kind "{kind}"',
            f'    language "{final_language}"',
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
        all_includes = []

        # Add consolidated global and platform-specific includes
        consolidated_includes = self._get_consolidated_includes()
        all_includes.extend(consolidated_includes)

        # Add library-specific include if specified
        if 'include' in lib:
            all_includes.append(lib['include'])

        # Add tree-sitter includes
        for lib_name in ['tree-sitter', 'tree-sitter-lambda']:
            if lib_name in self.external_libraries:
                include_path = self.external_libraries[lib_name]['include']
                if include_path:
                    all_includes.append(include_path)

        # Add other external library includes
        for lib_name in dependencies:
            if lib_name in self.external_libraries:
                include_path = self.external_libraries[lib_name]['include']
                if include_path:
                    all_includes.append(include_path)

        # Remove duplicates while preserving order
        seen = set()
        unique_includes = []
        for include in all_includes:
            if include and include not in seen:
                unique_includes.append(include)
                seen.add(include)

        if unique_includes:
            self.premake_content.extend([
                '    includedirs {',
            ])
            for include_path in unique_includes:
                self.premake_content.append(f'        "{include_path}",')
            self.premake_content.extend([
                '    }',
                '    '
            ])

        # Add build options
        base_compiler, _ = self._get_compiler_info()
        build_opts = self._get_build_options(base_compiler)

        # Check if this project has mixed C/C++ files
        c_files_present = any(f.endswith('.c') for f in source_files)
        cpp_files_present = any(f.endswith('.cpp') for f in source_files)

        if c_files_present and cpp_files_present and final_language == "C++":
            # Mixed project: use file-specific build options
            c_build_opts = build_opts + ['-std=c17']  # Default C standard for mixed projects
            cpp_standard = self._get_cpp_standard(lib)
            cpp_build_opts = build_opts + [f'-std={cpp_standard}']

            # C file build options
            self.premake_content.extend([
                '    filter "files:**.c"',
                '        buildoptions {',
            ])
            for opt in c_build_opts:
                self.premake_content.append(f'            "{opt}",')
            self.premake_content.extend([
                '        }',
                '    ',
                '    filter "files:**.cpp"',
                '        buildoptions {',
            ])
            for opt in cpp_build_opts:
                self.premake_content.append(f'            "{opt}",')
            self.premake_content.extend([
                '        }',
                '    ',
                '    filter {}',
                '    '
            ])
        else:
            # Pure language project: use global build options
            if final_language == "C++":
                cpp_standard = self._get_cpp_standard(lib)
                build_opts.append(f'-std={cpp_standard}')
            elif final_language == "C":
                # Add C standard support - use the corrected standard for split projects
                if project_name.endswith('-c'):
                    # Use the corrected C standard for split C projects
                    build_opts.append(f'-std={standard}')
                else:
                    # For regular C projects, get from library config
                    _, c_standard, _ = self._get_language_info(lib)
                    build_opts.append(f'-std={c_standard}')

            # Add Windows DLL export flags for lambda-input-full projects - moved to linkoptions
            # if (self.use_windows_config and link_type == 'dynamic' and
            #     project_name.startswith('lambda-input-full')):
            #     build_opts.extend(['-Wl,--export-all-symbols', '-Wl,--enable-auto-import'])

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

            # Process special_flags for frameworks
            special_flags_frameworks = []
            special_flags = lib.get('special_flags', '')
            if special_flags:
                # Parse special_flags to extract frameworks
                flag_parts = special_flags.split()
                i = 0
                while i < len(flag_parts):
                    if flag_parts[i] == '-framework' and i + 1 < len(flag_parts):
                        framework_name = flag_parts[i + 1]
                        special_flags_frameworks.append(framework_name)
                        i += 2
                    else:
                        i += 1

            # Add libdirs if we have dependencies
            self.premake_content.extend([
                '    libdirs {',
            ])

            # Add platform-specific library paths
            if self.use_windows_config:
                self.premake_content.extend([
                    '        "/mingw64/lib",',
                    '        "win-native-deps/lib",',
                    '        "build/lib",',
                ])
            else:
                self.premake_content.extend([
                    '        "/opt/homebrew/lib",',
                    '        "/usr/local/lib",',
                    '        "build/lib",',
                ])

            self.premake_content.extend([
                '    }',
                '    '
            ])

            # Add linkoptions for external static libraries
            if external_deps:
                static_libs = []
                frameworks = []
                dynamic_libs = []
                # Libraries that need to come after internal deps
                late_binding_lib_names = ['rpmalloc']

                for dep in external_deps:
                    if dep in self.external_libraries:
                        lib_info = self.external_libraries[dep]

                        # Skip libraries with link type "none"
                        if lib_info.get('link') == 'none':
                            continue

                        # Skip late-binding libraries here - they're added after links block
                        if self.use_windows_config and dep in late_binding_lib_names:
                            continue

                        lib_path = lib_info['lib']

                        if lib_info.get('link') == 'dynamic':
                            if lib_path.startswith('-framework '):
                                frameworks.append(lib_path.replace('-framework ', ''))
                            elif lib_path.startswith('-l'):
                                dynamic_libs.append(lib_path.replace('-l', ''))
                            else:
                                dynamic_libs.append(lib_path)
                        else:
                            # Static library - handle all static libraries consistently
                            if not lib_path.startswith('/') and not lib_path.startswith('-l'):
                                lib_path = f"../../{lib_path}"
                            static_libs.append(lib_path)

                # Add static libraries to linkoptions
                if static_libs:
                    self.premake_content.append('    linkoptions {')
                    for lib_path in static_libs:
                        self.premake_content.append(f'        "{lib_path}",')
                    # Add Windows system libraries that static libraries depend on
                    if self.use_windows_config:
                        # Windows networking libraries for CURL
                        self.premake_content.extend([
                            '        "-lws2_32",',
                            '        "-lwsock32",',
                            '        "-lwinmm",',
                            '        "-lcrypt32",',
                            '        "-lbcrypt",',
                            '        "-ladvapi32",',
                        ])
                    # Add Windows DLL export flags for lambda-input-full projects
                    if (self.use_windows_config and link_type == 'dynamic' and project_name.startswith('lambda-input-full')):
                        self.premake_content.extend([
                            '        "-Wl,--export-all-symbols",',
                            '        "-Wl,--enable-auto-import",',
                        ])
                    self.premake_content.extend([
                        '    }',
                        '    '
                    ])

                # Add frameworks, dynamic libraries, and internal libraries to links
                if frameworks or dynamic_libs or internal_deps or special_flags_frameworks:
                    self.premake_content.append('    links {')
                    # Add frameworks from external dependencies (macOS only)
                    if not self.use_windows_config:
                        for framework in frameworks:
                            self.premake_content.append(f'        "{framework}.framework",')
                    # Add frameworks from special_flags (macOS only)
                    if not self.use_windows_config:
                        for framework in special_flags_frameworks:
                            self.premake_content.append(f'        "{framework}.framework",')
                    for dyn_lib in dynamic_libs:
                        self.premake_content.append(f'        "{dyn_lib}",')
                    for dep in internal_deps:
                        self.premake_content.append(f'        "{dep}",')
                    # Add late-binding static libraries (must come after internal deps)
                    if self.use_windows_config:
                        late_binding_lib_names = ['rpmalloc']
                        for dep_name in late_binding_lib_names:
                            if dep_name in external_deps and dep_name in self.external_libraries:
                                # Use :static modifier to link static library by name
                                self.premake_content.append(f'        "{dep_name}:static",')
                    self.premake_content.extend([
                        '    }',
                        '    '
                    ])
            else:
                # Add links for internal libraries and special_flags frameworks only
                if internal_deps or special_flags_frameworks:
                    self.premake_content.append('    links {')
                    # Add frameworks from special_flags (macOS only)
                    if not self.use_windows_config:
                        for framework in special_flags_frameworks:
                            self.premake_content.append(f'        "{framework}.framework",')
                    for dep in internal_deps:
                        self.premake_content.append(f'        "{dep}",')
                    self.premake_content.extend([
                        '    }',
                        '    '
                    ])

        # Add Windows DLL export flags for lambda-input-full projects as separate linkoptions
        if (self.use_windows_config and link_type == 'dynamic' and
            project_name.startswith('lambda-input-full')):
            if project_name == 'lambda-input-full-cpp':
                # Use .def file for C++ project for precise symbol export
                self.premake_content.extend([
                    '    linkoptions {',
                    '        "-Wl,--output-def,lambda-input-full-cpp.def",',
                    '        "../../lambda-input-full-cpp.def",',
                    '    }',
                    '    '
                ])
            else:
                # Use export-all-symbols for C project
                self.premake_content.extend([
                    '    linkoptions {',
                    '        "-Wl,--export-all-symbols",',
                    '        "-Wl,--enable-auto-import",',
                    '    }',
                    '    '
                ])

        # Add platform-specific defines
        platform_defines = []
        if self.use_windows_config:
            # Add Windows-specific defines for static linking
            platforms_config = self.config.get('platforms', {})
            windows_config = platforms_config.get('windows', {})
            windows_flags = windows_config.get('flags', [])

            # Extract define flags from Windows flags
            for flag in windows_flags:
                if flag.startswith('D'):
                    platform_defines.append(flag[1:])  # Remove 'D' prefix

            # Add critical static linking defines for Windows
            platform_defines.extend(['UTF8PROC_STATIC', 'CURL_STATICLIB'])

        # Add target-specific defines
        target_defines = lib.get('defines', []) if isinstance(lib, dict) else []
        all_defines = platform_defines + target_defines

        if all_defines:
            self.premake_content.extend([
                '    defines {',
            ])
            for define in all_defines:
                self.premake_content.append(f'        "{define}",')
            self.premake_content.extend([
                '    }',
                '    '
            ])

        # Add macOS frameworks for library projects
        if self.use_macos_config:
            self.premake_content.extend([
                '    -- Add macOS frameworks',
                '    linkoptions {'
            ])

            # Add macOS frameworks using linkoptions
            for lib_name in self.external_libraries:
                if self.external_libraries[lib_name].get('link') == 'dynamic':
                    lib_flag = self.external_libraries[lib_name]['lib']
                    if lib_flag.startswith('-framework '):
                        self.premake_content.append(f'        "{lib_flag}",')

            self.premake_content.extend([
                '    }',
                '    '
            ])

        # Automatically add C++ standard library for C++ library projects
        if needs_cpp_stdlib and not self.use_windows_config:
            # Add C++ standard library based on platform
            cpp_stdlib = 'c++' if self.use_macos_config else 'stdc++'
            self.premake_content.extend([
                '    -- Automatically added C++ standard library',
                '    links {',
                f'        "{cpp_stdlib}",',
                '    }',
                '    '
            ])

        self.premake_content.append('')

    def _create_wrapper_project(self, lib_name: str, sub_projects: List[str], lib: Dict[str, Any] = None) -> None:
        """Create a wrapper project that combines multiple sub-projects"""
        # Determine library type based on link attribute
        link_type = lib.get('link', 'static') if lib else 'static'

        # Force DLL for lambda-input-full on Windows to avoid static library dependency issues
        # if self.use_windows_config and lib_name.startswith('lambda-input-full'):
        #     link_type = 'dynamic'

        kind = "SharedLib" if link_type == 'dynamic' else "StaticLib"

        self.premake_content.extend([
            f'project "{lib_name}"',
            f'    kind "{kind}"',
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

        # Determine library type based on link attribute
        link_type = lib.get('link', 'static')
        kind = "SharedLib" if link_type == 'dynamic' else "StaticLib"

        self.premake_content.extend([
            f'project "{lib_name}"',
            f'    kind "{kind}"',
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
        ])

        # Add include directories - start with consolidated includes
        all_includes = []

        # Add consolidated global and platform-specific includes
        consolidated_includes = self._get_consolidated_includes()
        all_includes.extend(consolidated_includes)

        # Add default lib/mem-pool/include for meta-libraries
        all_includes.append("lib/mem-pool/include")

        # Add external library include paths for meta-library dependencies
        inline_libs = ['strbuf', 'strview', 'mem-pool', 'datetime', 'string', 'num_stack', 'url']
        external_deps = [dep for dep in dependencies if dep not in inline_libs]
        for lib_name in external_deps:
            if lib_name in self.external_libraries:
                include_path = self.external_libraries[lib_name]['include']
                if include_path:
                    all_includes.append(include_path)

        # Remove duplicates while preserving order
        seen = set()
        unique_includes = []
        for include in all_includes:
            if include and include not in seen:
                unique_includes.append(include)
                seen.add(include)

        if unique_includes:
            self.premake_content.extend([
                '    includedirs {',
            ])
            for include_path in unique_includes:
                self.premake_content.append(f'        "{include_path}",')
            self.premake_content.extend([
                '    }',
                '    '
            ])

        # Add library dependencies for meta-libraries
        if dependencies:
            external_deps = [dep for dep in dependencies if dep not in inline_libs]
            if external_deps:
                self.premake_content.extend([
                    '    libdirs {',
                ])

                # Add platform-specific library paths
                if self.use_windows_config:
                    self.premake_content.extend([
                        '        "/mingw64/lib",',
                        '        "win-native-deps/lib",',
                    ])
                else:
                    self.premake_content.extend([
                        '        "/opt/homebrew/lib",',
                        '        "/usr/local/lib",',
                    ])

                self.premake_content.extend([
                    '    }',
                    '    ',
                    '    links {',
                ])
                for dep in external_deps:
                    self.premake_content.append(f'        "{dep}",')
                self.premake_content.extend([
                    '    }',
                    '    '
                ])

        self.premake_content.extend([
            '    buildoptions {',
        ])

        # Get compiler-specific build options
        base_compiler, _ = self._get_compiler_info()
        build_opts = self._get_build_options(base_compiler)

        # Add Windows DLL export flags for lambda-input-full projects - moved to linkoptions
        # if (self.use_windows_config and link_type == 'dynamic' and
        #     lib_name.startswith('lambda-input-full')):
        #     build_opts.extend(['-Wl,--export-all-symbols', '-Wl,--enable-auto-import'])

        for opt in build_opts:
            self.premake_content.append(f'        "{opt}",')

        self.premake_content.extend([
            '    }',
            '    '
        ])

        # Add defines from target configuration
        target_defines = lib.get('defines', [])
        if target_defines:
            self.premake_content.extend([
                '    defines {',
            ])
            for define in target_defines:
                self.premake_content.append(f'        "{define}",')
            self.premake_content.extend([
                '    }',
                '    '
            ])

        # Add Windows DLL export flags for lambda-input-full projects as separate linkoptions
        if (self.use_windows_config and lib.get('link') == 'dynamic' and
            lib_name.startswith('lambda-input-full')):
            if lib_name == 'lambda-input-full-cpp':
                # Use .def file for C++ project for precise symbol export
                self.premake_content.extend([
                    '    linkoptions {',
                    '        "-Wl,--output-def,lambda-input-full-cpp.def",',
                    '        "../../lambda-input-full-cpp.def",',
                    '    }',
                    '    '
                ])
            else:
                # Use export-all-symbols for C project
                self.premake_content.extend([
                    '    linkoptions {',
                    '        "-Wl,--export-all-symbols",',
                    '        "-Wl,--enable-auto-import",',
                    '    }',
                    '    '
                ])


        self.premake_content.append('')

    def generate_test_projects(self) -> None:
        """Generate test executable projects from test suites or test_projects"""
        # Handle new test_projects format (for cross-platform builds)
        test_projects = self.config.get('test_projects', [])
        if test_projects:
            for test_project in test_projects:
                self._generate_test_project(test_project)
            return

        # Handle old test configuration format (backward compatibility)
        test_config = self.config.get('test', {})
        if not test_config:
            # No test configuration, skip test generation
            return

        test_suites = test_config.get('test_suites', [])

        for suite in test_suites:
            suite_name = suite.get('suite', '')
            suite_type = suite.get('type', '')

            # Skip disabled test suites
            if suite.get('disabled', False):
                print(f"Skipping disabled test suite: {suite_name}")
                continue

            # Determine test framework type for this suite
            is_criterion_suite = False

            if suite_type == 'library':
                is_criterion_suite = True
            else:
                # Assume it's Criterion/library tests
                is_criterion_suite = True

            # Skip problematic test suites that have linking issues in the old test system
            # Note: All test suites now enabled with proper dependencies
            problematic_suites = []  # All test suites now enabled
            if suite_name in problematic_suites:
                continue

            self._generate_test_suite(suite)

    def _generate_test_project(self, project: Dict[str, Any]) -> None:
        """Generate a single test project from test_projects configuration"""
        name = project.get('name', '')
        kind = project.get('kind', 'ConsoleApp')
        language = project.get('language', 'C')
        files = project.get('files', [])
        links = project.get('links', [])

        if not name or not files:
            return

        self.premake_content.extend([
            '',
            f'project "{name}"',
            f'    kind "{kind}"',
            f'    language "{language}"',
            f'    targetdir "{self.config.get("target_dir", "test")}"',
            f'    objdir "build/obj/%{{prj.name}}"',
            f'    targetextension ".exe"',
            '',
            f'    files {{',
        ])

        # Add source files
        for file in files:
            self.premake_content.append(f'        "{file}",')

        self.premake_content.extend([
            '    }',
            '',
        ])

        # Add include directories using consolidated includes
        all_includes = []

        # Add consolidated global and platform-specific includes
        consolidated_includes = self._get_consolidated_includes()
        all_includes.extend(consolidated_includes)

        # Add legacy include_dirs
        include_dirs = self.config.get('include_dirs', [])
        all_includes.extend(include_dirs)

        # Remove duplicates while preserving order
        seen = set()
        unique_includes = []
        for include in all_includes:
            if include and include not in seen:
                unique_includes.append(include)
                seen.add(include)

        if unique_includes:
            self.premake_content.append('    includedirs {')
            for include_dir in unique_includes:
                self.premake_content.append(f'        "{include_dir}",')
            self.premake_content.extend([
                '    }',
                '',
            ])

        self.premake_content.extend([
            '    libdirs {'
        ])

        # Add library directories
        for lib_dir in self.config.get('lib_dirs', []):
            self.premake_content.append(f'        "{lib_dir}",')

        self.premake_content.extend([
            '    }',
            '',
            '    links {'
        ])

        # Add linked libraries
        for link in links:
            self.premake_content.append(f'        "{link}",')

        for lib in self.config.get('libraries', []):
            self.premake_content.append(f'        "{lib}",')

        self.premake_content.extend([
            '    }',
            '',
        ])

        # Add build options
        cflags = self.config.get('cflags', [])
        cxxflags = self.config.get('cxxflags', [])

        if cflags:
            self.premake_content.extend([
                '    filter "files:**.c"',
                '        buildoptions {'
            ])
            for flag in cflags:
                self.premake_content.append(f'            "{flag}",')
            self.premake_content.extend([
                '        }',
                ''
            ])

        if cxxflags:
            self.premake_content.extend([
                '    filter "files:**.cpp"',
                '        buildoptions {'
            ])
            for flag in cxxflags:
                self.premake_content.append(f'            "{flag}",')
            self.premake_content.extend([
                '        }',
                ''
            ])

        # Add defines
        defines = self.config.get('defines', [])
        if defines:
            self.premake_content.extend([
                '    defines {'
            ])
            for define in defines:
                self.premake_content.append(f'        "{define}",')
            self.premake_content.extend([
                '    }',
                ''
            ])

        # Add platform-specific settings
        platform = self.config.get('platform', '')
        if platform == 'Linux_x64':
            self.premake_content.extend([
                f'    filter "platforms:{platform}"',
                '        system "linux"',
                '        architecture "x64"',
                '        toolset "gcc"',
                '        gccprefix "x86_64-linux-gnu-"',
                ''
            ])

        self.premake_content.extend([
            '    filter {}',
            ''
        ])

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
                # Skip tests marked with "skip": true or "disabled": true
                if test.get('skip', False) or test.get('disabled', False):
                    test_name = test.get('name', test.get('source', 'unknown'))
                    print(f"Skipping test: {test_name}")
                    continue

                source = test.get('source', '')
                binary_name = test.get('binary', '').replace('.exe', '')
                dependencies = test.get('dependencies', [])
                libraries = test.get('libraries', [])
                defines = test.get('defines', [])
                test_name_override = test.get('name', '')

                # Check for gtest flag and add gtest libraries
                if test.get('gtest', False):
                    # Add gtest and gtest_main to libraries if not already present
                    if 'gtest' not in libraries:
                        libraries.append('gtest')
                    if 'gtest_main' not in libraries:
                        libraries.append('gtest_main')

                # Enhanced support for test-specific flags and additional sources
                test_special_flags = test.get('special_flags', special_flags)  # Test-specific flags override suite flags
                additional_sources = test.get('additional_sources', [])  # New field for extra source files

                if not source or not binary_name:
                    continue

                # Avoid subdirectory structure by flattening test names
                # Extract just the filename from binary path to prevent double prefixes
                import os
                binary_basename = os.path.basename(binary_name)
                test_name = binary_basename.replace('/', '_')
                if test_name.startswith('test_'):
                    test_name = test_name[5:]  # Remove 'test_' prefix only
                test_name = f"test_{test_name}"
                additional_files = test.get('additional_files', [])

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

                self._generate_single_test(test_name, test_file_path, dependencies, test_special_flags, cpp_flags, libraries, defines, additional_files, additional_sources, binary_name)

    def _generate_single_test(self, test_name: str, test_file_path: str, dependencies: List[str],
                             special_flags: str, cpp_flags: str, libraries: List[str] = None, defines: List[str] = None, additional_files: List[str] = None, additional_sources: List[str] = None, target_name: str = None) -> None:
        """Generate a single test project"""
        if libraries is None:
            libraries = []
        if defines is None:
            defines = []
        if additional_files is None:
            additional_files = []
        if additional_sources is None:
            additional_sources = []

        source = test_file_path
        language = "C" if source.endswith('.c') else "C++"

        self.premake_content.extend([
            f'project "{test_name}"',
            '    kind "ConsoleApp"',
            f'    language "{language}"',
            '    targetdir "test"',
            '    objdir "build/obj/%{prj.name}"',
        ])

        # Use custom target name if provided, otherwise use the project name
        if target_name:
            # Remove .exe extension and extract just the filename for targetname
            import os
            clean_target_name = os.path.basename(target_name).replace('.exe', '')
            self.premake_content.append(f'    targetname "{clean_target_name}"')

        self.premake_content.extend([
            '    targetextension ".exe"',
            '    ',
            '    files {',
            f'        "{test_file_path}",',
        ])

        # Add additional source files if specified (NEW FEATURE)
        for additional_source in additional_sources:
            self.premake_content.append(f'        "{additional_source}",')

        # Add additional files if specified
        for additional_file in additional_files:
            self.premake_content.append(f'        "{additional_file}",')

        self.premake_content.extend([
            '    }',
            '    '
        ])

        # Add include directories using consolidated includes and parsed library definitions
        all_includes = []

        # Add consolidated global and platform-specific includes first
        consolidated_includes = self._get_consolidated_includes()
        all_includes.extend(consolidated_includes)

        # Add default mem-pool include for tests
        all_includes.append("lib/mem-pool/include")

        # Add external library include paths from parsed definitions
        for lib_name, lib_info in self.external_libraries.items():
            # Skip libraries with link type "none"
            if lib_info.get('link') == 'none':
                continue

            if lib_info['include']:
                all_includes.append(lib_info['include'])

        # Add platform-specific include paths
        platform = self.config.get('platform', 'macOS')
        if platform == 'Linux_x64':
            # Linux cross-compilation paths
            all_includes.extend([
                "linux-deps/include",
                "linux-deps/include/ncurses",
            ])
        elif self.use_windows_config:
            # Windows/MSYS2 paths
            all_includes.extend([
                "/mingw64/include",
                "win-native-deps/include",
            ])
        else:
            # macOS paths (default)
            # IMPORTANT: /opt/homebrew/include must come before /usr/local/include
            # to ensure Homebrew's gtest headers are found before any system-wide
            # gtest installation that may have incompatible declarations
            all_includes.extend([
                "/opt/homebrew/include",
                "/usr/local/include",
            ])

        # Remove duplicates while preserving order
        seen = set()
        unique_includes = []
        for include in all_includes:
            if include and include not in seen:
                unique_includes.append(include)
                seen.add(include)

        if unique_includes:
            self.premake_content.extend([
                '    includedirs {',
            ])
            for include_path in unique_includes:
                self.premake_content.append(f'        "{include_path}",')
            self.premake_content.extend([
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
        self.premake_content.append('    libdirs {')

        # Add platform-specific library paths
        platform = self.config.get('platform', 'macOS')
        if platform == 'Linux_x64':
            # Linux cross-compilation paths
            self.premake_content.extend([
                '        "linux-deps/lib",',
                '        "build/lib",',
            ])
        elif self.use_linux_config:
            # Native Linux paths
            self.premake_content.extend([
                '        "/usr/local/lib",',
                '        "/usr/local/lib/aarch64-linux-gnu",',
                '        "/usr/lib/aarch64-linux-gnu",',
                '        "build/lib",',
            ])
        elif self.use_windows_config:
            # Windows/MSYS2 paths
            self.premake_content.extend([
                '        "/mingw64/lib",',
                '        "win-native-deps/lib",',
                '        "build/lib",',
            ])
        else:
            # macOS paths (default)
            self.premake_content.extend([
                '        "/opt/homebrew/lib",',
                '        "/opt/homebrew/Cellar/criterion/2.4.2_2/lib",',
                '        "/usr/local/lib",',
                '        "build/lib",',
            ])

        self.premake_content.extend([
            '    }',
            '    '
        ])

        # Add library dependencies
        self.premake_content.append('    links {')

        # Initialize test frameworks tracking
        test_frameworks_added = []

        # Process dependencies first
        if dependencies:
            for dep in dependencies:
                if dep == 'criterion':
                    self.premake_content.append('        "criterion",')
                elif dep == 'lambda-lib':
                    # Lambda-lib contains all the core libraries
                    self.premake_content.append('        "lambda-lib",')
                elif dep in ['lambda-runtime-full', 'lambda-input-full']:
                    # Special handling for MIR, Lambda, Math, and Markup tests
                    if ('mir' in test_name.lower() or 'lambda' in test_name.lower() or 'math' in test_name.lower() or 'markup' in test_name.lower()) and dep == 'lambda-runtime-full':
                        # All tests only need the -cpp versions (C++ project includes all C files)
                        self.premake_content.append('        "lambda-runtime-full-cpp",')
                        self.premake_content.append('        "lambda-input-full-cpp",')
                    else:
                        # Regular tests: only need -cpp version (C++ project includes all C files)
                        self.premake_content.append(f'        "{dep}-cpp",')

                    # Add lambda-lib which now contains all core dependencies
                    self.premake_content.append('        "lambda-lib",')

        # Add test framework libraries
        # Add libraries specified in the test configuration
        if libraries:
            for lib in libraries:
                if lib == 'criterion':
                    self.premake_content.append('        "criterion",')
                    # Add Criterion dependencies (required on macOS with Homebrew)
                    self.premake_content.append('        "nanomsg",')
                    self.premake_content.append('        "git2",')
                    test_frameworks_added.append('criterion')
                elif lib == 'gtest':
                    # Don't add to links - let static library handling in linkoptions handle it
                    # This avoids duplicate linking (-lgtest + /path/libgtest.a)
                    test_frameworks_added.append('gtest')
                elif lib == 'gtest_main':
                    # Don't add to links - let static library handling in linkoptions handle it
                    test_frameworks_added.append('gtest')
                else:
                    # Handle other libraries
                    if lib == 'c++fs':
                        # On macOS with modern Clang, filesystem is part of libc++ and doesn't need separate linking
                        # Only add if not on macOS
                        platform = self.config.get('platform', 'macOS')
                        if platform != 'macOS' and 'darwin' not in platform.lower():
                            self.premake_content.append('        "stdc++fs",')
                        # On macOS, we don't need to link anything for filesystem
                    else:
                        # Check if this library is defined in external_libraries first
                        if lib in self.external_libraries:
                            lib_info = self.external_libraries[lib]
                            # Skip libraries with link type "none"
                            if lib_info.get('link') != 'none':
                                # If it's a static library, it will be handled in linkoptions later
                                # If it's dynamic, add it to links
                                if lib_info.get('link') == 'dynamic':
                                    self.premake_content.append(f'        "{lib}",')
                                # Static libraries are handled in the linkoptions section below
                        else:
                            # Library not found in external definitions, assume it's a system library
                            self.premake_content.append(f'        "{lib}",')

            # Special handling for lambda tests that use Catch2
            if (test_name and 'lambda' in test_name.lower() and 'catch2' in test_name.lower() and
                libraries and any(lib in ['Catch2Main', 'Catch2', 'Catch2Maind', 'Catch2d'] for lib in libraries)):
                # Ensure catch2 is marked as added for lambda tests using Catch2
                if 'catch2' not in test_frameworks_added:
                    test_frameworks_added.append('catch2')

        # Only add criterion to test executables if no other test framework is specified
        if 'criterion' not in test_frameworks_added and 'catch2' not in test_frameworks_added and 'gtest' not in test_frameworks_added:
            self.premake_content.append('        "criterion",')
            # Add Criterion dependencies (required on macOS with Homebrew)
            self.premake_content.append('        "nanomsg",')
            self.premake_content.append('        "git2",')

        # Close the links block
        self.premake_content.extend([
            '    }',
            '    '
        ])

        # Add external library linkoptions for test-specific libraries
        if libraries:
            external_static_libs = []
            late_static_libs = []  # Static libs that need to come after lambda-lib (link order)
            for lib_name in libraries:
                if lib_name in self.external_libraries:
                    lib_info = self.external_libraries[lib_name]

                    # Skip libraries with link type "none"
                    if lib_info.get('link') == 'none':
                        continue

                    if lib_info.get('link') == 'static':
                        lib_path = lib_info['lib']
                        if not lib_path.startswith('/'):
                            lib_path = f"../../{lib_path}"

                        # Special handling for tree-sitter libraries - add them to external_static_libs (linkoptions)
                        if lib_name in ['tree-sitter', 'tree-sitter-lambda', 'tree-sitter-latex-math']:
                            external_static_libs.append(lib_path)
                        # On Linux/Windows, static libs need to come AFTER lambda-lib in link order
                        # because lambda-lib has unresolved symbols that these libs provide
                        elif (self.use_linux_config or self.use_windows_config) and lib_name in ['rpmalloc', 'utf8proc']:
                            late_static_libs.append((lib_name, lib_path))
                        else:
                            external_static_libs.append(lib_path)

            # Note: tree-sitter libraries are now handled via external_static_libs (linkoptions)
            # No longer adding them to links block which causes incorrect -l flags

            # Add late static libraries to links block (must come after lambda-lib on Linux)
            if late_static_libs:
                # Re-open the links block
                self.premake_content[-2] = '    '  # Remove the closing brace line
                self.premake_content.pop()  # Remove the empty line

                for lib_name, lib_path in late_static_libs:
                    if lib_name == 'rpmalloc':
                        # Use :librpmalloc.a syntax (libdir already set up)
                        self.premake_content.append('        ":librpmalloc.a",')
                    elif lib_name == 'utf8proc':
                        # Use :libutf8proc.a syntax (path in libdir /usr/lib/aarch64-linux-gnu)
                        self.premake_content.append('        ":libutf8proc.a",')
                    else:
                        self.premake_content.append(f'        "{lib_path}",')

                # Close the links block again
                self.premake_content.extend([
                    '    }',
                    '    '
                ])

            if external_static_libs:
                self.premake_content.append('    linkoptions {')
                for lib_path in external_static_libs:
                    self.premake_content.append(f'        "{lib_path}",')
                self.premake_content.extend([
                    '    }',
                    '    '
                ])

        # Add external library paths for linking when lambda-runtime-full or lambda-input-full are used
        has_input_full_deps = any(dep in ['lambda-runtime-full', 'lambda-input-full'] or dep.startswith('lambda-runtime-full-') or dep.startswith('lambda-input-full-') for dep in dependencies)
        if has_input_full_deps:
            self.premake_content.extend([
                '    linkoptions {',
            ])

            # Add --start-group only on Linux for circular dependency resolution
            if self.use_linux_config:
                self.premake_content.append('        "-Wl,--start-group",')

            # Add static external libraries with explicit paths like the main lambda program
            if self.use_windows_config:
                # Windows: allow multiple definitions to avoid duplicate _Unwind_Resume from libgcc_eh
                # This is needed because lambda-input-full DLL includes exception handling code
                self.premake_content.append('        "-Wl,--allow-multiple-definition",')

                # Windows: use the same explicit paths as the main lambda program
                windows_lib_paths = [
                    "../../lambda/tree-sitter/libtree-sitter.a",
                    "../../lambda/tree-sitter-lambda/libtree-sitter-lambda.a",
                    "../../win-native-deps/lib/libmir.a",
                    "/mingw64/lib/libmpdec.a",
                    "../../win-native-deps/lib/libutf8proc.a",
                    "/mingw64/lib/libmbedtls.a",
                    "/mingw64/lib/libmbedx509.a",
                    "/mingw64/lib/libmbedcrypto.a",
                ]
                for lib_path in windows_lib_paths:
                    self.premake_content.append(f'        "{lib_path}",')
                # Add dynamic libraries
                self.premake_content.extend([
                    '        "-lcurl",',
                    '        "-lnghttp2",',
                    '        "-lz",',
                    '        "-lbz2",',
                    '        "-lfreetype",',
                    '        "-lpng",',
                ])
                # Non-Windows: use the original approach with external library definitions
                # If lambda-input-full is a dependency, we need to include curl/ssl/crypto for proper linking
                # since static libraries don't propagate their dependencies in Premake
                # Note: Lambda's custom curl was built with external nghttp2 dependency
                base_libs = ['mpdec', 'utf8proc', 'mir', 'nghttp2', 'curl', 'ssl', 'crypto']
                # Add platform-specific readline library
                base_libs.append('libedit')

                for lib_name in base_libs:
                    if lib_name in self.external_libraries:
                        # Skip if this library should be dynamic
                        if self.external_libraries[lib_name].get('link') == 'dynamic':
                            continue
                        lib_path = self.external_libraries[lib_name]['lib']
                        # Convert to relative path from build directory
                        if not lib_path.startswith('/'):
                            lib_path = f"../../{lib_path}"

                        # Force load nghttp2 on macOS to ensure curl can find its symbols
                        if lib_name == 'nghttp2' and not self.use_windows_config and not self.use_linux_config:
                            self.premake_content.append(f'        "-Wl,-force_load,{lib_path}",')
                        else:
                            self.premake_content.append(f'        "{lib_path}",')

            # Add --end-group only on Linux for circular dependency resolution
            if self.use_linux_config:
                self.premake_content.append('        "-Wl,--end-group",')

            self.premake_content.extend([
                '    }',
                '    ',
                '    -- Add dynamic libraries',
                '    links {'
            ])

            # Add dynamic libraries (not frameworks)
            for lib_name in self.external_libraries:
                if self.external_libraries[lib_name].get('link') == 'dynamic':
                    lib_flag = self.external_libraries[lib_name]['lib']
                    # Skip frameworks (they go in linkoptions)
                    if lib_flag.startswith('-framework '):
                        continue
                    if lib_flag.startswith('-l'):
                        lib_flag = lib_flag[2:]  # Remove -l prefix
                    self.premake_content.append(f'        "{lib_flag}",')

            # Add system libraries that libedit depends on (Linux only)
            if not self.use_windows_config:
                self.premake_content.append('        "ncurses",')

            # Add rpmalloc for Linux (must come after shared libraries that depend on it)
            # Added INSIDE the links block so it comes AFTER the shared library in link order
            if self.use_linux_config:
                if 'rpmalloc' in self.external_libraries:
                    # Use :librpmalloc.a to link static library (libdir already set)
                    self.premake_content.append('        ":librpmalloc.a",')

            self.premake_content.extend([
                '    }',
                '    ',
            ])

            self.premake_content.extend([
                '    -- Add tree-sitter libraries using linkoptions to append to LIBS section',
                '    linkoptions {',
            ])

            self.premake_content.extend([
                '    }',
                '    ',
                '    -- Add macOS frameworks',
                '    linkoptions {'
            ])

            # Add macOS frameworks using linkoptions
            for lib_name in self.external_libraries:
                if self.external_libraries[lib_name].get('link') == 'dynamic':
                    lib_flag = self.external_libraries[lib_name]['lib']
                    if lib_flag.startswith('-framework '):
                        self.premake_content.append(f'        "{lib_flag}",')

            self.premake_content.extend([
                '    }',
                '    '
            ])

        # Add build options based on source file type
        is_cpp_test = source.endswith('.cpp')
        base_compiler, _ = self._get_compiler_info()
        build_opts = self._get_build_options(base_compiler)

        if is_cpp_test:
            if cpp_flags:
                build_opts.append(cpp_flags)
            # Add special flags for C++ tests
            if special_flags:
                # Handle special_flags as either string or list
                if isinstance(special_flags, str):
                    flag_list = special_flags.split()
                else:
                    flag_list = special_flags

                for flag in flag_list:
                    if flag == '-lstdc++':
                        self.premake_content.extend([
                            '    links { "stdc++" }',
                            '    '
                        ])
                    else:
                        build_opts.append(flag)
        else:
            # For C files, check if special flags contain a std flag
            has_std_flag = False
            if special_flags:
                # Handle special_flags as either string or list
                if isinstance(special_flags, str):
                    flag_list = special_flags.split()
                else:
                    flag_list = special_flags

                for flag in flag_list:
                    if flag.startswith('-std='):
                        has_std_flag = True
                    build_opts.append(flag)

            # Only add default C99 standard if no std flag was specified
            if not has_std_flag:
                build_opts.append('-std=c99')

        # Note: Previously had a workaround for gtest string_view PrintStringTo issue
        # This was fixed by ensuring /opt/homebrew/include comes before /usr/local/include
        # in build_lambda_config.json, so the correct gtest headers are found first

        self.premake_content.extend([
            '    buildoptions {',
        ])
        for opt in build_opts:
            self.premake_content.append(f'        "{opt}",')

        self.premake_content.extend([
            '    }',
            '    ',
        ])

        # Add tree-sitter libraries as linker options for tests with lambda-input-full dependencies
        # Use platform-specific flags to force inclusion of all symbols from tree-sitter libraries
        if any(dep == 'lambda-input-full' for dep in dependencies):
            self.premake_content.extend([
                '    filter {}',
                '    linkoptions {',
            ])

            if self.use_linux_config:
                # Linux: use --whole-archive
                self.premake_content.append('        "-Wl,--whole-archive",')
                for lib_name in ['tree-sitter-lambda', 'tree-sitter']:
                    if lib_name in self.external_libraries:
                        lib_path = self.external_libraries[lib_name]['lib']
                        if not lib_path.startswith('/'):
                            lib_path = f"../../{lib_path}"
                        self.premake_content.append(f'        "{lib_path}",')
                self.premake_content.append('        "-Wl,--no-whole-archive",')
            elif self.use_macos_config:
                # macOS: use -force_load for each library
                for lib_name in ['tree-sitter-lambda', 'tree-sitter']:
                    if lib_name in self.external_libraries:
                        lib_path = self.external_libraries[lib_name]['lib']
                        if not lib_path.startswith('/'):
                            lib_path = f"../../{lib_path}"
                        self.premake_content.append(f'        "-Wl,-force_load,{lib_path}",')
            else:
                # Default: just link normally without forcing symbol inclusion
                for lib_name in ['tree-sitter-lambda', 'tree-sitter']:
                    if lib_name in self.external_libraries:
                        lib_path = self.external_libraries[lib_name]['lib']
                        if not lib_path.startswith('/'):
                            lib_path = f"../../{lib_path}"
                        self.premake_content.append(f'        "{lib_path}",')

            self.premake_content.extend([
                '    }',
                '    ',
            ])

        # Add AddressSanitizer for test projects only (not for main lambda.exe)
        # Disable on Windows since ASAN is not readily available in MINGW64
        platforms_config = self.config.get('platforms', {})
        disable_sanitizer = False
        if self.use_windows_config:
            windows_config = platforms_config.get('windows', {})
            disable_sanitizer = windows_config.get('disable_sanitizer', True)  # Default to True for Windows
        else:
            linux_config = platforms_config.get('linux', {})
            disable_sanitizer = linux_config.get('disable_sanitizer', False)

        if not disable_sanitizer:
            self.premake_content.extend([
                '    -- AddressSanitizer for test projects only',
                '    filter { "configurations:Debug", "not platforms:Linux_x64" }',
                '        buildoptions { "-fsanitize=address", "-fno-omit-frame-pointer" }',
                '        linkoptions { "-fsanitize=address" }',
                '    ',
                '    filter {}',
                '    ',
            ])

        self.premake_content.append('')

    def generate_main_program(self) -> None:
        """Generate the main Lambda program executable"""
        output = self.config.get('output', 'lambda.exe')
        source_files = self.config.get('source_files', [])
        source_dirs = self.config.get('source_dirs', [])
        dependencies = []

        # Extract main program dependencies from libraries
        for lib in self.config.get('libraries', []):
            # Handle both string and object formats
            if isinstance(lib, str):
                # String format: just library name
                if lib not in ['criterion']:  # Exclude test-only libraries
                    dependencies.append(lib)
            elif isinstance(lib, dict):
                lib_name = lib.get('name', '')
                if lib_name not in ['criterion']:  # Exclude test-only libraries
                    dependencies.append(lib_name)

        # Add platform-specific libraries for Windows
        # Platform-specific libraries may override global ones for correct ordering
        platforms_config = self.config.get('platforms', {})
        if self.use_windows_config:
            windows_config = platforms_config.get('windows', {})
            for lib in windows_config.get('libraries', []):
                lib_name = lib.get('name', '')
                if lib_name and lib_name not in ['criterion']:
                    # Remove from current position if it exists (to respect platform ordering)
                    if lib_name in dependencies:
                        dependencies.remove(lib_name)
                    dependencies.append(lib_name)

        # Add platform-specific libraries for Linux
        # Platform-specific libraries may override global ones for correct ordering
        if self.use_linux_config:
            linux_config = platforms_config.get('linux', {})
            for lib in linux_config.get('libraries', []):
                lib_name = lib.get('name', '')
                if lib_name and lib_name not in ['criterion']:
                    # Remove from current position if it exists (to respect platform ordering)
                    if lib_name in dependencies:
                        dependencies.remove(lib_name)
                    dependencies.append(lib_name)

        # Add platform-specific libraries for macOS
        import platform
        current_platform = platform.system()
        if current_platform == 'Darwin':
            macos_config = platforms_config.get('macos', {})
            for lib in macos_config.get('libraries', []):
                lib_name = lib.get('name', '')
                if lib_name and lib_name not in dependencies:
                    dependencies.append(lib_name)

        # NOTE: dev_libraries (ginac, cln, gmp, criterion, catch2) are NOT included
        # in the main program - they are only for development and testing

        # Remove .exe extension for project name and adjust for platform
        project_name = output.replace('.exe', '')

        # Use platform-specific target names
        target_name = output.replace('.exe', '')  # Use the config output name without .exe
        target_extension = '.exe'

        # Add files from source directories explicitly
        all_source_files = source_files[:]

        # Get global exclusions and additions first
        exclude_files = self.config.get('exclude_source_files', []).copy()
        additional_files = self.config.get('additional_source_files', []).copy()
        
        # Then add platform-specific exclusions and additions
        platforms_config = self.config.get('platforms', {})
        if self.use_windows_config:
            windows_config = platforms_config.get('windows', {})
            exclude_files.extend(windows_config.get('exclude_source_files', []))
            additional_files.extend(windows_config.get('additional_source_files', []))
        elif self.use_linux_config:
            linux_config = platforms_config.get('linux', {})
            exclude_files.extend(linux_config.get('exclude_source_files', []))
            additional_files.extend(linux_config.get('additional_source_files', []))
        elif self.use_macos_config:
            macos_config = platforms_config.get('macos', {})
            exclude_files.extend(macos_config.get('exclude_source_files', []))
            additional_files.extend(macos_config.get('additional_source_files', []))

        for source_dir in source_dirs:
            import glob
            c_pattern = f"{source_dir}/*.c"
            cpp_pattern = f"{source_dir}/*.cpp"

            # Find all C files (one level only, non-recursive)
            c_files = glob.glob(c_pattern, recursive=False)
            all_source_files.extend(c_files)

            # Find all C++ files (one level only, non-recursive)
            cpp_files = glob.glob(cpp_pattern, recursive=False)
            all_source_files.extend(cpp_files)

        # Remove excluded files
        if exclude_files:
            all_source_files = [f for f in all_source_files if f not in exclude_files]

        # Add additional platform-specific files
        if additional_files:
            all_source_files.extend(additional_files)

        self.premake_content.extend([
            f'project "{project_name}"',
            '    kind "ConsoleApp"',
            '    language "C++"',  # Use C++ as primary language since we have mixed sources
            '    targetdir "."',
            '    objdir "build/obj/%{prj.name}"',
            f'    targetname "{target_name}"',
            f'    targetextension "{target_extension}"',
            '    ',
            '    files {',
        ])

        # Add all source files explicitly
        for source in all_source_files:
            self.premake_content.append(f'        "{source}",')

        self.premake_content.extend([
            '    }',
            '    ',
        ])

        # Add include directories using consolidated includes
        all_includes = []

        # Add consolidated global and platform-specific includes first
        consolidated_includes = self._get_consolidated_includes()
        all_includes.extend(consolidated_includes)

        # Add default main program includes
        all_includes.extend([
            ".",
            "lambda/tree-sitter/lib/include",
            "lambda/tree-sitter-lambda/bindings/c",
            "lib/mem-pool/include",
        ])

        # Add external library include paths (excluding dev_libraries)
        dev_lib_names = {lib.get('name', '') if isinstance(lib, dict) else lib
                        for lib in self.config.get('dev_libraries', [])}

        for lib_name, lib_info in self.external_libraries.items():
            # Skip libraries with link type "none"
            if lib_info.get('link') == 'none':
                continue
            # Skip dev libraries
            if lib_name in dev_lib_names:
                continue
            if lib_info['include']:
                all_includes.append(lib_info['include'])

        # Remove duplicates while preserving order
        seen = set()
        unique_includes = []
        for include in all_includes:
            if include and include not in seen:
                unique_includes.append(include)
                seen.add(include)

        if unique_includes:
            self.premake_content.extend([
                '    includedirs {',
            ])
            for include_path in unique_includes:
                self.premake_content.append(f'        "{include_path}",')
            self.premake_content.extend([
                '    }',
                '    '
            ])

        self.premake_content.extend([
            '    libdirs {',
        ])

        # Add platform-specific library paths
        if self.use_windows_config:
            self.premake_content.extend([
                '        "/mingw64/lib",',
                '        "win-native-deps/lib",',
                '        "build/lib",',
            ])
        else:
            self.premake_content.extend([
                '        "/opt/homebrew/lib",',
                '        "/usr/local/lib",',
                '        "build/lib",',
            ])

        self.premake_content.extend([
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

                # Skip libraries with link type "none"
                if lib_info.get('link') == 'none':
                    continue

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
                    if not lib_path.startswith('/') and not lib_path.startswith('-l'):
                        lib_path = f"../../{lib_path}"
                    static_libs.append(lib_path)

        # Add static libraries to linkoptions
        if static_libs:
            self.premake_content.append('    linkoptions {')
            for lib_path in static_libs:
                self.premake_content.append(f'        "{lib_path}",')

            # Add platform-specific additional libraries for static linking
            # These are the same libraries that test projects include
            # Note: Lambda's custom curl was built with external nghttp2 dependency
            base_libs = ['mpdec', 'utf8proc', 'mir', 'nghttp2', 'curl', 'ssl', 'crypto', 'hpdf']
            # Add platform-specific readline library
            if self.use_windows_config:
                # Windows: skip readline/ncurses to avoid DLL dependencies
                pass
            else:
                base_libs.append('libedit')

            # Add these libraries if they're not already included and exist in external_libraries
            for lib_name in base_libs:
                if lib_name in self.external_libraries:
                    lib_info = self.external_libraries[lib_name]

                    # Skip libraries with link type "none"
                    if lib_info.get('link') == 'none':
                        continue

                    if lib_info.get('link') != 'dynamic':
                        lib_path = lib_info['lib']
                        if not lib_path.startswith('/') and lib_path:
                            lib_path = f"../../{lib_path}"
                        if lib_path and lib_path != "../../":
                            # Force load nghttp2 on macOS to ensure curl can find its symbols
                            if lib_name == 'nghttp2' and not self.use_windows_config and not self.use_linux_config:
                                self.premake_content.append(f'        "-Wl,-force_load,{lib_path}",')
                            else:
                                self.premake_content.append(f'        "{lib_path}",')

            # Add OpenGL libraries for Linux (must come after ThorVG static library)
            if self.use_linux_config:
                self.premake_content.append('        -- OpenGL and OpenMP libraries (required by ThorVG)')
                self.premake_content.append('        "-lGL",')
                self.premake_content.append('        "-lGLU",')
                self.premake_content.append('        "-lgomp",')

            self.premake_content.extend([
                '    }',
                '    '
            ])

        # Add platform-specific linker options
        output = self.config.get('output', 'lambda.exe')
        if 'linux' in output.lower():
            # Linux-specific static libraries
            self.premake_content.extend([
                '    -- Linux cross-compilation static libraries',
                '    filter "platforms:Linux_x64"',
                '        linkoptions {',
            ])

            # Add Linux static libraries from config
            linux_libs = []
            for dep in dependencies:
                if dep in self.external_libraries:
                    lib_info = self.external_libraries[dep]

                    # Skip libraries with link type "none"
                    if lib_info.get('link') == 'none':
                        continue

                    if lib_info.get('link') == 'static':
                        lib_path = lib_info['lib']
                        if not lib_path.startswith('/'):
                            lib_path = f"../../{lib_path}"
                        linux_libs.append(lib_path)

            for lib_path in linux_libs:
                self.premake_content.append(f'            "{lib_path}",')

            self.premake_content.extend([
                '        }',
                '    ',
                '    filter {}',
                '    '
            ])

        # Add dynamic libraries and frameworks (macOS only)
        # Always add dynamic libraries section for cross-platform compatibility
        self.premake_content.extend([
            '    -- Dynamic libraries',
            '    filter "platforms:native"',
            '        links {',
        ])

        # Add all dynamic libraries
        for lib in dynamic_libs:
            self.premake_content.append(f'            "{lib}",')

        # Add Windows system libraries if on Windows
        if self.use_windows_config:
            windows_dynamic_libs = []
            for dep in dependencies:
                if dep in self.external_libraries:
                    lib_info = self.external_libraries[dep]
                    if lib_info.get('link') == 'dynamic':
                        lib_flag = lib_info['lib']
                        if lib_flag.startswith('-l'):
                            lib_flag = lib_flag[2:]  # Remove -l prefix
                        if lib_flag not in dynamic_libs:
                            windows_dynamic_libs.append(lib_flag)

            for lib in windows_dynamic_libs:
                self.premake_content.append(f'            "{lib}",')

        self.premake_content.extend([
            '        }',
            '    '
        ])

        import platform
        current_platform = platform.system()

        # Only add frameworks on macOS
        if frameworks and current_platform == 'Darwin':
            self.premake_content.extend([
                '        linkoptions {',
            ])
            for framework in frameworks:
                self.premake_content.append(f'            "{framework}",')
            self.premake_content.extend([
                '        }',
                '    ',
                '    filter {}',
                '    '
            ])
        else:
            self.premake_content.extend([
                '    filter {}',
                '    '
            ])

        # Add build options with separate handling for C and C++ files
        base_compiler, _ = self._get_compiler_info()
        build_opts = self._get_build_options(base_compiler)

        self.premake_content.extend([
            '    buildoptions {',
        ])
        for opt in build_opts:
            self.premake_content.append(f'        "{opt}",')
        self.premake_content.extend([
            '    }',
            '    ',
            '    -- C++ specific options',
            '    filter "files:**.cpp"',
        ])
        cpp_standard = self._get_cpp_standard()
        self.premake_content.extend([
            f'        buildoptions {{ "-std={cpp_standard}" }}',
            '    ',
            '    -- C specific options',
            '    filter "files:**.c"',
            '        buildoptions { "-std=c99" }',
            '    ',
            '    filter {}',
            '    ',
            '    defines {',
            '        "_GNU_SOURCE",',
        ])

        # Add Windows-specific defines for the main lambda project
        if self.use_windows_config:
            self.premake_content.extend([
                '        "WIN32",',
                '        "_WIN32",',
                '        "NATIVE_WINDOWS_BUILD",',
                '        "CURL_STATICLIB",',
                '        "UTF8PROC_STATIC",',
            ])

        self.premake_content.extend([
            '    }',
            '    ',
            ''
        ])

    def generate_premake_file(self, output_path: str = "premake5.lua") -> None:
        """Generate the complete premake5.lua file"""
        print(f"DEBUG: Starting premake file generation, output_path={output_path}")

        # Determine platform from filename or current platform detection
        platform_name = "unknown"
        if "mac" in output_path.lower():
            platform_name = "macOS"
        elif "lin" in output_path.lower():
            platform_name = "Linux"
        elif "win" in output_path.lower():
            platform_name = "Windows"
        elif self.use_macos_config:
            platform_name = "macOS"
        elif self.use_linux_config:
            platform_name = "Linux"
        elif self.use_windows_config:
            platform_name = "Windows"

        self.premake_content = []

        # Add header comment with platform information
        self.premake_content.extend([
            f'-- Generated by utils/generate_premake.py for {platform_name}',
            '-- Lambda Build System Premake5 Configuration',
            f'-- Platform: {platform_name}',
            '-- DO NOT EDIT MANUALLY - Regenerate using: python3 utils/generate_premake.py',
            '',
        ])

        print(f"DEBUG: Added header comment for {platform_name}")

        # Generate all sections
        print("DEBUG: Generating workspace...")
        self.generate_workspace()
        print("DEBUG: Generating library projects...")
        self.generate_library_projects()
        print("DEBUG: Generating complex libraries...")
        self.generate_complex_libraries()
        print("DEBUG: Generating main program...")
        self.generate_main_program()
        print("DEBUG: Generating test projects...")
        self.generate_test_projects()

        print(f"DEBUG: Total premake content lines: {len(self.premake_content)}")

        # Write to file
        try:
            print(f"DEBUG: Attempting to write to {output_path}")
            with open(output_path, 'w') as f:
                content_str = '\n'.join(self.premake_content)
                f.write(content_str)
                print(f"DEBUG: Successfully wrote {len(content_str)} characters to {output_path}")
            print(f"Generated {platform_name} premake file: {output_path}")

        except IOError as e:
            print(f"Error writing {output_path}: {e}")
            sys.exit(1)

    def validate_config(self) -> bool:
        """Validate the JSON configuration"""
        required_sections = ['libraries']
        for section in required_sections:
            if section not in self.config:
                print(f"Error: Missing required section '{section}' in configuration")
                return False

        # Test section is optional for non-test builds (like Linux cross-compilation)
        if 'test' in self.config:
            test_config = self.config['test']
            if 'test_suites' not in test_config:
                print("Error: Missing 'test_suites' in test configuration")
                return False

        return True

def main():
    """Main entry point"""
    print(f"DEBUG: sys.argv = {sys.argv}")

    config_file = "build_lambda_config.json"
    output_file = None  # Will be determined based on platform
    explicit_platform = None

    # Parse command line arguments
    i = 1
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg in ['--output', '-o'] and i + 1 < len(sys.argv):
            output_file = sys.argv[i + 1]
            i += 2
        elif arg in ['--config', '-c'] and i + 1 < len(sys.argv):
            config_file = sys.argv[i + 1]
            i += 2
        elif arg in ['--platform', '-p'] and i + 1 < len(sys.argv):
            explicit_platform = sys.argv[i + 1].lower()
            i += 2
        elif arg.endswith('.json'):
            config_file = arg
            i += 1
        elif arg.endswith('.lua'):
            output_file = arg
            i += 1
        else:
            # Assume it's a config file if not a lua file
            if i == 1:
                config_file = arg
            elif i == 2 and output_file is None:
                output_file = arg
            i += 1

    # Determine platform if not explicitly set via output filename
    if output_file is None:
        # Auto-detect platform and generate appropriate filename
        current_platform = platform.system()
        if explicit_platform:
            if explicit_platform in ['mac', 'macos', 'darwin']:
                output_file = "premake5.mac.lua"
            elif explicit_platform in ['linux', 'lin']:
                output_file = "premake5.lin.lua"
            elif explicit_platform in ['windows', 'win']:
                output_file = "premake5.win.lua"
            else:
                print(f"Error: Unknown platform '{explicit_platform}'. Use 'mac', 'linux', or 'windows'")
                sys.exit(1)
        elif current_platform == 'Darwin':
            output_file = "premake5.mac.lua"
        elif current_platform == 'Linux':
            output_file = "premake5.lin.lua"
        elif current_platform == 'Windows' or current_platform.startswith('MINGW') or current_platform.startswith('MSYS'):
            output_file = "premake5.win.lua"
        else:
            print(f"Warning: Unknown platform '{current_platform}', defaulting to premake5.mac.lua")
            output_file = "premake5.mac.lua"

    print(f"DEBUG: Final config_file={config_file}, output_file={output_file}")

    # Generate Premake5 configuration
    generator = PremakeGenerator(config_file, explicit_platform)
    generator.parse_config()

    if not generator.validate_config():
        sys.exit(1)

    generator.generate_premake_file(output_file)
    print(f"Premake5 migration completed successfully!")
    print(f"Generated platform-specific file: {output_file}")
    print(f"Next steps:")
    print(f"  1. Run: premake5 gmake2 --file={output_file}")
    print(f"  2. Run: make -C build/premake config=debug_native")
    print(f"")
    print(f"To generate for other platforms, use:")
    print(f"  python3 utils/generate_premake.py --platform mac")
    print(f"  python3 utils/generate_premake.py --platform linux")
    print(f"  python3 utils/generate_premake.py --platform windows")

if __name__ == "__main__":
    main()
