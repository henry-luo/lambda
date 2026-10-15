import native: .mod_binary_native_schema

// Count fields may follow the pointer field; the host binding supplies its extent.
type Fixed : binary & {layout: 'native', binding: 'fixture.Fixed', version: 'v1'} {
    values: u32[4] & {pointer: 'native'},
    bytes: binary[0]? & {pointer: 'native'}
}
type UnicodeCount : binary & {layout: 'native', binding: 'fixture.UnicodeCount'} {
    values: u8[n_β] & {pointer: 'native'}, n_β: i32
}
type Keyword : binary & {layout: 'native', binding: 'fixture.Keyword'} {
    values: u8[offset] & {pointer: 'native'}, offset: i32
}
[native.NativeEntry is type, native.NativeBatch is type, native.NativeNumbers is type, Fixed is type,
 UnicodeCount is type, Keyword is type, [1, 2, 3] is int[ 3 ], [1, 2, 3, 4, 5, 6, 7, 8, 9, 10] is int[1_0]]
