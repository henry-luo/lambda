#!/bin/bash
# Here-document and here-string tests

# Basic here-document
cat <<EOF
Hello from heredoc
Line two
Line three
EOF

# Here-document with variable expansion
name="Lambda"
cat <<EOF
Hello, $name!
Version: $(echo "1.0")
EOF

# Here-document with no expansion (quoted delimiter)
cat <<'EOF'
No expansion: $name
No substitution: $(echo test)
EOF

# Here-string
cat <<< "single line here-string"

# Here-string with variable
greeting="Howdy"
cat <<< "$greeting, partner!"
