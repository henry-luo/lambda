# Lambda Runtime Data Management

Lambda runtime uses the following design/convention to represent and manage its runtime data:
- for simple scalar types: LMD_TYPE_NULL, LMD_TYPE_BOOL, LMD_TYPE_INT
	- they are packed into Item, with high bits set to TypeId;
- for compound scalar types: LMD_TYPE_INT64, LMD_TYPE_FLOAT, LMD_TYPE_DTIME, LMD_TYPE_DECIMAL, LMD_TYPE_SYMBOL, LMD_TYPE_STRING, LMD_TYPE_BINARY
	- they are packed into item as a tagged pointer. It's a pointer to the actual data, with high bits set to TypeId.
	- LMD_TYPE_INT64, LMD_TYPE_FLOAT, LMD_TYPE_DTIME are stored in a special num_stack at runtime;
	- LMD_TYPE_DECIMAL, LMD_TYPE_SYMBOL, LMD_TYPE_STRING, LMD_TYPE_BINARY are allocated from heap, and reference counted;
- for container types: LMD_TYPE_LIST, LMD_TYPE_RANGE, LMD_TYPE_ARRAY_INT, LMD_TYPE_ARRAY, LMD_TYPE_MAP, LMD_TYPE_ELEMENT
	- they are direct pointers to the container data.
	- all containers extends struct Container, that starts with field TypeId;
	- they are heap allocated, and reference counted;
- Lambda map/LMD_TYPE_MAP, uses a packed struct:
	- its list of fields are defined as a linked list of ShapeEntry;
	- and the actual data are stored as a packed struct;
- Lambda element/LMD_TYPE_ELEMENT, extends Lambda list/LMD_TYPE_LIST, and it's also a map/LMD_TYPE_MAP at the same time;
	- note that it can be casted as List directly, but not Map directly;
- can use get_type_id() function to get the TypeId of an Item in a general manner;

### Coding Guidelines
- Start comments in lowercase.
- **Add debug logging** for development and troubleshooting.
- **Test with comprehensive nested data structures** and use timeout (default: 5s) to catch hangs early
- **Back up the file** before major refactoring or rewrite. Remove the backup at the end of successful refactoring or rewrite.