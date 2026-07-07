// trim() unit tests — covers empty-string results

// basic trim
"1. trim both sides:"
trim("  hello  ")
"2. trim no whitespace:"
trim("hello")
"3. trim left only:"
trim("  hello")
"4. trim right only:"
trim("hello  ")

// all-whitespace trims to an empty string, not absence
"5. trim spaces → empty:"
trim("   ") == ""
"6. trim tab → empty:"
trim("\t") == ""
"7. trim newline → empty:"
trim("\n") == ""
"8. trim mixed ws → empty:"
trim(" \t\n ") == ""
"9. type of trimmed ws:"
type(trim("  "))

// null input → error (not a string)
"10. trim null → error:"
type(trim(null))

// trim_start
"11. trim_start:"
trim_start("  hello  ")
"12. trim_start all ws → empty:"
trim_start("   ") == ""
"13. type trim_start ws:"
type(trim_start("\t\n"))

// trim_end
"14. trim_end:"
trim_end("  hello  ")
"15. trim_end all ws → empty:"
trim_end("   ") == ""
"16. type trim_end ws:"
type(trim_end("\t\n"))

// edge cases
"17. trim single space → empty:"
trim(" ") == ""
"18. trim single char:"
trim("a")
"19. inner ws preserved:"
trim("  hello world  ")

// symbol trim
"20. trim symbol:"
trim('hello')
"21. trim symbol type:"
type(trim('hello'))
