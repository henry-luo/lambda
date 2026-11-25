^cut '-d,' '-f1,2' input.csv
    | lines --strict
    | where {|line| $line starts-with 'ERROR,'}
    | each {|line| $line | split row ',' | get 1 | str uppercase}
    | to text
    | ^cat
