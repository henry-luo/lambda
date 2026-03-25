# F4: sorted with key/reverse
words = ["banana", "pie", "Washington", "a"]
print(sorted(words, key=lambda s: len(s)))
print(sorted([3, 1, 2], reverse=True))
print(sorted([5, 2, 8, 1], key=lambda x: -x))
