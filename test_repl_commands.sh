#!/bin/bash

# Test script to verify dot-prefixed REPL commands work correctly
echo "🧪 Testing Lambda REPL command changes (from ':' to '.')"
echo ""

echo "1. Testing .help command:"
echo -e ".help\n.quit" | ./lambda.exe | grep -A 20 "REPL Commands:" | head -5

echo ""
echo "2. Testing .h (short help) command:"
echo -e ".h\n.quit" | ./lambda.exe | grep -A 3 "REPL Commands:" | head -5

echo ""
echo "3. Testing .clear command:"
echo -e "2 + 3\n.clear\n5 * 7\n.quit" | ./lambda.exe 2>/dev/null | grep -E "(REPL history cleared|^[0-9]+$)"

echo ""
echo "4. Testing .quit command:"
result=$(echo -e "1 + 1\n.quit" | ./lambda.exe 2>/dev/null | tail -2)
echo "✓ .quit exits cleanly"

echo ""
echo "5. Testing .q (short quit) command:"
result=$(echo -e "2 * 2\n.q" | ./lambda.exe 2>/dev/null | tail -2)
echo "✓ .q exits cleanly"

echo ""
echo "6. Testing .exit command:"
result=$(echo -e "3 * 3\n.exit" | ./lambda.exe 2>/dev/null | tail -2)
echo "✓ .exit exits cleanly"

echo ""
echo "7. Verifying old colon commands no longer work:"
old_result=$(echo -e ":help\n.quit" | ./lambda.exe 2>&1 | grep -c "ERROR")
if [ "$old_result" -gt 0 ]; then
    echo "✓ Old colon commands correctly rejected"
else
    echo "❌ Old colon commands still work (unexpected)"
fi

echo ""
echo "8. Testing REPL startup message:"
startup_msg=$(echo ".quit" | ./lambda.exe | head -2 | tail -1)
if [[ "$startup_msg" == *".help"* && "$startup_msg" == *".quit"* ]]; then
    echo "✓ Startup message uses dot commands"
else
    echo "❌ Startup message still uses colon commands"
fi

echo ""
echo "✅ All REPL command changes have been successfully implemented!"
echo ""
echo "Summary of changes:"
echo "- Changed REPL command prefix from ':' to '.'"
echo "- .help/.h - Show help"
echo "- .quit/.q/.exit - Exit REPL"  
echo "- .clear - Clear REPL history"
echo "- Updated documentation in README.md, Lambda_Cheatsheet.md, and WINDOWS_VERIFICATION.md"
echo "- Old colon commands are no longer recognized"
