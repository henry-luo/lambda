{
  description = "Lambda Script - A pure functional scripting language development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        
        # Custom MIR build
        mir = pkgs.stdenv.mkDerivation rec {
          pname = "mir";
          version = "2024-01-01";
          
          src = pkgs.fetchFromGitHub {
            owner = "vnmakarov";
            repo = "mir";
            rev = "e4ff75d8c8c8c7d5b8b1c8b1c8b1c8b1c8b1c8b1"; # Replace with actual commit
            sha256 = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="; # Replace with actual hash
          };

          nativeBuildInputs = with pkgs; [ gnumake gcc ];
          
          buildPhase = ''
            make -j$NIX_BUILD_CORES
          '';
          
          installPhase = ''
            mkdir -p $out/lib $out/include
            cp libmir.a $out/lib/ || true
            cp mir.h $out/include/ || true
            cp mir-gen.h $out/include/ || true
            # Copy all header files
            find . -name "*.h" -exec cp {} $out/include/ \;
          '';
          
          meta = with pkgs.lib; {
            description = "MIR (Medium Internal Representation) JIT compiler";
            homepage = "https://github.com/vnmakarov/mir";
            license = licenses.mit;
            platforms = platforms.unix;
          };
        };

        # Tree-sitter with specific version
        tree-sitter-cli-024 = pkgs.tree-sitter.overrideAttrs (oldAttrs: rec {
          version = "0.24.7";
          src = pkgs.fetchFromGitHub {
            owner = "tree-sitter";
            repo = "tree-sitter";
            rev = "v${version}";
            sha256 = "sha256-BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB="; # Replace with actual hash
          };
        });

        # Build scripts
        setup-lambda-deps = pkgs.writeShellScriptBin "setup-lambda-deps" ''
          set -e
          echo "🔧 Setting up Lambda Script dependencies..."
          
          # Build tree-sitter library
          if [ ! -f "lambda/tree-sitter/libtree-sitter.a" ]; then
            echo "Building tree-sitter library..."
            cd lambda/tree-sitter
            make clean || true
            make libtree-sitter.a
            cd - > /dev/null
            echo "✅ Tree-sitter library built"
          else
            echo "✅ Tree-sitter library already exists"
          fi
          
          # Build tree-sitter-lambda
          if [ ! -f "lambda/tree-sitter-lambda/libtree-sitter-lambda.a" ]; then
            echo "Building tree-sitter-lambda..."
            cd lambda/tree-sitter-lambda
            make clean || true
            make libtree-sitter-lambda.a
            cd - > /dev/null
            echo "✅ Tree-sitter-lambda built"
          else
            echo "✅ Tree-sitter-lambda already exists"
          fi
          
          echo ""
          echo "🎉 All dependencies are ready!"
          echo "You can now run 'make build' to compile Lambda Script"
        '';

        clean-lambda-deps = pkgs.writeShellScriptBin "clean-lambda-deps" ''
          echo "🧹 Cleaning Lambda build artifacts..."
          
          # Clean tree-sitter build files
          if [ -d "lambda/tree-sitter" ]; then
            cd lambda/tree-sitter
            make clean 2>/dev/null || true
            cd - > /dev/null
          fi
          
          # Clean tree-sitter-lambda build files
          if [ -d "lambda/tree-sitter-lambda" ]; then
            cd lambda/tree-sitter-lambda
            make clean 2>/dev/null || true
            cd - > /dev/null
          fi
          
          # Clean build directories
          rm -rf build/ build_debug/ build_temp/ 2>/dev/null || true
          
          # Clean object files
          find . -name "*.o" -type f -delete 2>/dev/null || true
          
          echo "✅ Cleanup completed"
        '';

        validate-setup = pkgs.writeShellScriptBin "validate-setup" ''
          echo "🔍 Validating Lambda Script development environment..."
          echo ""
          
          # Check essential tools
          echo "Essential tools:"
          for tool in make gcc git cmake pkg-config premake5 node npm npx; do
            if command -v "$tool" >/dev/null 2>&1; then
              echo "  ✅ $tool"
            else
              echo "  ❌ $tool (missing)"
            fi
          done
          
          # Check macOS-specific tools
          if [[ "$OSTYPE" == "darwin"* ]]; then
            echo ""
            echo "macOS-specific tools:"
            for tool in clang llvm-config; do
              if command -v "$tool" >/dev/null 2>&1; then
                echo "  ✅ $tool"
              else
                echo "  ❌ $tool (missing)"
              fi
            done
          fi
          echo ""
          
          # Check libraries
          echo "Libraries (checking via pkg-config where available):"
          for lib in openssl libcurl nghttp2; do
            if pkg-config --exists "$lib" 2>/dev/null; then
              version=$(pkg-config --modversion "$lib" 2>/dev/null || echo "unknown")
              echo "  ✅ $lib ($version)"
            else
              echo "  ⚠️  $lib (not found via pkg-config, but may be available)"
            fi
          done
          echo ""
          
          # Check tree-sitter
          echo "Tree-sitter:"
          if command -v tree-sitter >/dev/null 2>&1; then
            version=$(tree-sitter --version 2>/dev/null || echo "unknown")
            echo "  ✅ tree-sitter CLI ($version)"
          else
            echo "  ❌ tree-sitter CLI"
          fi
          
          if npx tree-sitter-cli@0.24.7 --version >/dev/null 2>&1; then
            echo "  ✅ tree-sitter-cli@0.24.7 via npx"
          else
            echo "  ⚠️  tree-sitter-cli@0.24.7 via npx (may download on first use)"
          fi
          echo ""
          
          # Check project-specific builds
          echo "Project builds:"
          if [ -f "lambda/tree-sitter/libtree-sitter.a" ]; then
            echo "  ✅ tree-sitter library"
          else
            echo "  ❌ tree-sitter library (run: setup-lambda-deps)"
          fi
          
          if [ -f "lambda/tree-sitter-lambda/libtree-sitter-lambda.a" ]; then
            echo "  ✅ tree-sitter-lambda library"
          else
            echo "  ❌ tree-sitter-lambda library (run: setup-lambda-deps)"
          fi
          echo ""
          
          echo "🎯 Run 'setup-lambda-deps' to build missing components"
        '';

      in
      {
        devShells.default = pkgs.mkShell {
          name = "lambda-script-dev";
          
          buildInputs = with pkgs; [
            # Essential build tools
            gnumake
            gcc
            git
            cmake
            pkg-config
            coreutils
            premake5
            
            # Node.js ecosystem
            nodejs
            tree-sitter
            
            # Core libraries
            libmpdecimal
            utf8proc
            openssl_3
            curl
            nghttp2
            
            # HTML/XML parsing
            lexbor
            
            # Testing frameworks  
            criterion
            
            # PDF generation
            libharu
            
            # JIT compilation
            mir  # Use system package or custom build above
            
            # Development tools
            gdb
            valgrind
            
            # Utilities
            jq
            unzip
            wget
            which
            
            # Custom scripts
            setup-lambda-deps
            clean-lambda-deps
            validate-setup
          ] ++ pkgs.lib.optionals pkgs.stdenv.isDarwin [
            # macOS-specific tools
            clang
            llvm
          ];

          shellHook = ''
            echo "🚀 Lambda Script development environment (Nix Flake)"
            echo ""
            echo "📋 Available commands:"
            echo "  setup-lambda-deps  - Build tree-sitter libraries"
            echo "  clean-lambda-deps  - Clean build artifacts"  
            echo "  validate-setup     - Check environment setup"
            echo "  make build         - Build Lambda Script"
            echo "  make test          - Run tests"
            echo ""
            echo "🔧 Environment:"
            echo "  Nix store: $NIX_STORE"
            echo "  System: ${system}"
            echo ""
            
            # Set up environment paths for build system
            export PKG_CONFIG_PATH="${pkgs.libmpdecimal}/lib/pkgconfig:${pkgs.utf8proc}/lib/pkgconfig:${pkgs.criterion}/lib/pkgconfig:${pkgs.openssl_3}/lib/pkgconfig:${pkgs.curl}/lib/pkgconfig:${pkgs.nghttp2}/lib/pkgconfig:$PKG_CONFIG_PATH"
            
            # Ensure timeout command is available
            export PATH="${pkgs.coreutils}/bin:$PATH"
            
            # Helpful aliases
            alias ll='ls -la'
            alias tree='${pkgs.tree}/bin/tree'
            
            echo "✅ Environment ready! Run 'validate-setup' to check everything."
          '';

          # Build flags for native compilation
          NIX_CFLAGS_COMPILE = toString [
            "-I${pkgs.libmpdecimal}/include"
            "-I${pkgs.utf8proc}/include"
            "-I${pkgs.criterion}/include"
            "-I${pkgs.openssl_3.dev}/include"
            "-I${pkgs.curl.dev}/include"
            "-I${pkgs.nghttp2.dev}/include"
            "-I${pkgs.lexbor}/include"
            "-I${pkgs.libharu}/include"
          ];

          NIX_LDFLAGS = toString [
            "-L${pkgs.libmpdecimal}/lib"
            "-L${pkgs.utf8proc}/lib"
            "-L${pkgs.criterion}/lib"
            "-L${pkgs.openssl_3.out}/lib"
            "-L${pkgs.curl.out}/lib"
            "-L${pkgs.nghttp2.out}/lib"
            "-L${pkgs.lexbor}/lib"
            "-L${pkgs.libharu}/lib"
          ];
        };

        # Default package (could be the lambda binary itself)
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "lambda-script";
          version = "0.1.0";
          src = ./.;
          
          nativeBuildInputs = with pkgs; [ gnumake gcc premake5 ];
          buildInputs = with pkgs; [
            libmpdecimal utf8proc criterion
            openssl_3 curl nghttp2 lexbor libharu mir
          ];
          
          buildPhase = ''
            # Build tree-sitter libraries first
            cd lambda/tree-sitter
            make libtree-sitter.a
            cd ../tree-sitter-lambda
            make libtree-sitter-lambda.a
            cd ../..
            
            # Build main project
            make build
          '';
          
          installPhase = ''
            mkdir -p $out/bin
            cp lambda.exe $out/bin/lambda || cp lambda $out/bin/lambda
          '';
        };
      });
}