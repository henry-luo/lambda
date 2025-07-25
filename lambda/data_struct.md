# Lambda Runtime Data Structures

Lambda runtime uses the following to represent its runtime data:
- for simple scalar types: LMD_TYPE_NULL, LMD_TYPE_BOOL, LMD_TYPE_INT
	- they are packed into Item, with high bits set to TypeId;
- for compound scalar types: LMD_TYPE_INT64, LMD_TYPE_FLOAT, LMD_TYPE_DECIMAL, LMD_TYPE_DTIME, LMD_TYPE_SYMBOL, LMD_TYPE_STRING, LMD_TYPE_BINARY
	- they are packed into item as a tagged pointer. It's a pointe to the actual data, with high bits set to TypeId.
- for container types: LMD_TYPE_LIST, LMD_TYPE_RANGE, LMD_TYPE_ARRAY_INT, LMD_TYPE_ARRAY, LMD_TYPE_MAP, LMD_TYPE_ELEMENT
	- they are direct pointers to the container data.
	- all containers extends struct Container, that starts with field TypeId;
- can use get_type_id() function to get the TypeId of an item in a general manner;
- Lambda map/LMD_TYPE_MAP, uses a packed struct:
	- its list of fields are defined as a linked list of ShapeEntry;
	- and the actual data are stored as a packed struct;
- Lambda element/LMD_TYPE_ELEMENT, extends Lambda list/LMD_TYPE_LIST, and it's also a map/LMD_TYPE_MAP at the same time;
	- note that it can be casted as List directly, but not Map directly;