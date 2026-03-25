#!/bin/bash
# Case statement tests

# Simple case
lang="bash"
case $lang in
    bash)
        echo "Bourne Again Shell"
        ;;
    python)
        echo "Python"
        ;;
    javascript)
        echo "JavaScript"
        ;;
    *)
        echo "Unknown"
        ;;
esac

# Case with patterns
ext=".js"
case $ext in
    .sh)
        echo "Shell script"
        ;;
    .py)
        echo "Python script"
        ;;
    .js)
        echo "JavaScript file"
        ;;
    .ls)
        echo "Lambda script"
        ;;
esac

# Case with OR patterns
animal="cat"
case $animal in
    dog|cat)
        echo "pet"
        ;;
    lion|tiger)
        echo "wild"
        ;;
esac

# Case with default
value="unknown"
case $value in
    yes)
        echo "affirmative"
        ;;
    no)
        echo "negative"
        ;;
    *)
        echo "unrecognized"
        ;;
esac

# Numeric case
code=404
case $code in
    200)
        echo "OK"
        ;;
    404)
        echo "Not Found"
        ;;
    500)
        echo "Server Error"
        ;;
esac
