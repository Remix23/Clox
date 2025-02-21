### Test system

- [ ]

### VM:

- [ ] More efficient line encoding (ch: 14)
- [ ]Allow support for more constants than 256 (add OP_CONSTANT_LONG)
- [ ] String interpolation as in python: f string with f" {} "
- [ ] Implement reallocate without the std malloc, realloc, free
- [ ] Dynamically resized stack
- [ ] Hash Map for various types \[ arbitrary type with hash well defined \]
- [ ] Hash table benchmark with different tweaks (conflicts resolution, tombstones?, hash func, growth factor)

### Interesing notes

- Finite state machines -> Trie data structure
- Dragon book: the compiler designer [Dragon Book](https://en.wikipedia.org/wiki/Compilers:_Principles,_Techniques,_and_Tools):
  - converting REGEX to DFA (deterministic finite automaton)
- Heap data struture
- Visitor pattern?
- [Handles are better pointers](https://floooh.github.io/2018/06/17/handles-vs-pointers.html)
- [Book of Shaders](https://thebookofshaders.com/02/)

### Chalenges:

##### Chapter 16:

1. Introduce string interpolation
2. Introduce token: GENERIC_INDENTIFIER token that when parsing consumes the bounding '<' abc '>' and if not found returns an error
3. Contextual keywords:
4. For the grammar some expressions (e.g. declarations) aren't allowed everywhere (disntiction between declaration and other statements)
