// test/lib/test_hashmap_helpers_gtest.cpp - tests for lib/hashmap_helpers
#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "../../lib/hashmap_helpers.h"
}
#include "../../lib/hashmap_typed.hpp"

namespace {

// inline char array key
struct ArrEntry {
    char name[64];
    int value;
};
HASHMAP_DEFINE_STRKEY(arr_entry, struct ArrEntry, name)

// pointer-to-cstr key
struct PtrEntry {
    const char* name;
    int value;
};
HASHMAP_DEFINE_STRKEY(ptr_entry, struct PtrEntry, name)

// pointer identity key
struct PtrIdEntry {
    void* key;
    int value;
};
HASHMAP_DEFINE_PTRKEY(ptr_id_entry, struct PtrIdEntry, key)

struct OwnedEntry { char name[16]; int* counter; };
HASHMAP_DEFINE_STRKEY(owned_entry, struct OwnedEntry, name)

// int64-keyed entry
struct I64Entry { int64_t key; int value; };
HASHMAP_DEFINE_INTKEY(i64_entry, struct I64Entry, key)

// uint32-keyed entry
struct U32Entry { uint32_t id; const char* tag; };
HASHMAP_DEFINE_INTKEY(u32_entry, struct U32Entry, id)

// length-prefix string key (raw ptr + len fields)
struct LenStrEntry { const char* name; size_t name_len; int value; };
HASHMAP_DEFINE_LENSTRKEY(len_str_entry, struct LenStrEntry, name, name_len)

struct Field2Entry { uint64_t id; const char* tag; int value; };
HASHMAP_DEFINE_FIELD2_KEY(field2_entry, struct Field2Entry, id, tag)

struct Field3Entry { uint64_t id; const char* tag; const char* state; int value; };
HASHMAP_DEFINE_FIELD3_KEY(field3_entry, struct Field3Entry, id, tag, state)

struct TypedCStrEntry { const char* name; int value; };
typedef TypedHashMap<TypedCStrEntry,
    HashMapCStrMemberKeyOps<TypedCStrEntry, &TypedCStrEntry::name>> TypedCStrMap;

struct TypedIdentityEntry { void* target; uint64_t signature; int value; };
typedef TypedHashMap<TypedIdentityEntry,
    HashMapIdentity2MemberKeyOps<TypedIdentityEntry, &TypedIdentityEntry::target,
        &TypedIdentityEntry::signature>> TypedIdentityMap;

}  // namespace

TEST(HashmapHelpersTest, StrKeyInlineArray) {
    struct hashmap* m = arr_entry_new(0);
    ASSERT_NE(m, nullptr);

    ArrEntry a; strcpy(a.name, "alpha"); a.value = 1;
    ArrEntry b; strcpy(b.name, "beta");  b.value = 2;
    hashmap_set(m, &a);
    hashmap_set(m, &b);
    EXPECT_EQ(hashmap_count(m), 2u);

    ArrEntry probe; strcpy(probe.name, "alpha");
    const ArrEntry* found = (const ArrEntry*)hashmap_get(m, &probe);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 1);

    strcpy(probe.name, "missing");
    EXPECT_EQ(hashmap_get(m, &probe), nullptr);

    hashmap_free(m);
}

TEST(HashmapHelpersTest, StrKeyPtrField) {
    struct hashmap* m = ptr_entry_new(0);
    ASSERT_NE(m, nullptr);

    PtrEntry a{"alpha", 1};
    PtrEntry b{"beta",  2};
    hashmap_set(m, &a);
    hashmap_set(m, &b);

    PtrEntry probe{"alpha", 0};
    const PtrEntry* found = (const PtrEntry*)hashmap_get(m, &probe);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 1);

    // different storage, same contents -> still equal
    char buf[8] = "alpha";
    PtrEntry probe2{buf, 0};
    found = (const PtrEntry*)hashmap_get(m, &probe2);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 1);

    hashmap_free(m);
}

TEST(HashmapHelpersTest, PtrKey) {
    struct hashmap* m = ptr_id_entry_new(0);
    ASSERT_NE(m, nullptr);

    int x = 0, y = 0;
    PtrIdEntry a{&x, 10};
    PtrIdEntry b{&y, 20};
    hashmap_set(m, &a);
    hashmap_set(m, &b);

    PtrIdEntry probe{&x, 0};
    const PtrIdEntry* found = (const PtrIdEntry*)hashmap_get(m, &probe);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 10);

    probe.key = &y;
    found = (const PtrIdEntry*)hashmap_get(m, &probe);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 20);

    int z = 0;
    probe.key = &z;
    EXPECT_EQ(hashmap_get(m, &probe), nullptr);

    hashmap_free(m);
}

TEST(TypedHashMapTest, CStrMemberKey) {
    TypedCStrMap map = {};
    ASSERT_TRUE(map.init(0));

    TypedCStrEntry first{"alpha", 1};
    TypedCStrEntry second{"beta", 2};
    EXPECT_EQ(map.set(first), nullptr);
    EXPECT_EQ(map.set(second), nullptr);

    char key_storage[] = "alpha";
    TypedCStrEntry query{key_storage, 0};
    const TypedCStrEntry* found = map.get(query);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 1);

    TypedCStrEntry replacement{"alpha", 3};
    const TypedCStrEntry* replaced = map.set(replacement);
    ASSERT_NE(replaced, nullptr);
    EXPECT_EQ(replaced->value, 1);
    EXPECT_EQ(map.count(), 2u);
    map.destroy();
}

TEST(TypedHashMapTest, CompositeIdentityKey) {
    int first_target = 0;
    int second_target = 0;
    HashMap* raw = TypedIdentityMap::create(0, 0x1234u, 0x5678u);
    ASSERT_NE(raw, nullptr);

    TypedIdentityEntry first{&first_target, 9, 10};
    TypedIdentityEntry second{&second_target, 9, 20};
    EXPECT_EQ(TypedIdentityMap::set(raw, first), nullptr);
    EXPECT_EQ(TypedIdentityMap::set(raw, second), nullptr);

    TypedIdentityEntry query{&second_target, 9, 0};
    const TypedIdentityEntry* found = TypedIdentityMap::get(raw, query);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 20);

    EXPECT_NE(TypedIdentityMap::erase(raw, query), nullptr);
    EXPECT_EQ(TypedIdentityMap::get(raw, query), nullptr);
    hashmap_free(raw);
}

TEST(HashmapHelpersTest, IntKeyInt64) {
    struct hashmap* m = i64_entry_new(0);
    ASSERT_NE(m, nullptr);
    I64Entry a{1, 10};
    I64Entry b{42, 20};
    I64Entry c{-7, 30};
    hashmap_set(m, &a);
    hashmap_set(m, &b);
    hashmap_set(m, &c);
    EXPECT_EQ(hashmap_count(m), 3u);

    I64Entry probe{42, 0};
    const I64Entry* found = (const I64Entry*)hashmap_get(m, &probe);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 20);

    probe.key = -7;
    found = (const I64Entry*)hashmap_get(m, &probe);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 30);

    probe.key = 999;
    EXPECT_EQ(hashmap_get(m, &probe), nullptr);
    hashmap_free(m);
}

TEST(HashmapHelpersTest, IntKeyUint32) {
    struct hashmap* m = u32_entry_new(0);
    ASSERT_NE(m, nullptr);
    U32Entry a{1u, "one"};
    U32Entry b{2u, "two"};
    hashmap_set(m, &a);
    hashmap_set(m, &b);

    U32Entry probe{1u, nullptr};
    const U32Entry* found = (const U32Entry*)hashmap_get(m, &probe);
    ASSERT_NE(found, nullptr);
    EXPECT_STREQ(found->tag, "one");
    hashmap_free(m);
}

TEST(HashmapHelpersTest, LenStrKey) {
    struct hashmap* m = len_str_entry_new(0);
    ASSERT_NE(m, nullptr);

    // insert three keys that are NOT NUL-terminated at their boundary
    const char* source = "alpha|beta|gamma";  // single buffer, three slices
    LenStrEntry a{source + 0, 5, 1};   // "alpha"
    LenStrEntry b{source + 6, 4, 2};   // "beta"
    LenStrEntry c{source + 11, 5, 3};  // "gamma"
    hashmap_set(m, &a);
    hashmap_set(m, &b);
    hashmap_set(m, &c);
    EXPECT_EQ(hashmap_count(m), 3u);

    // lookup using a different storage but same bytes
    char buf[8] = "beta";
    LenStrEntry probe{buf, 4, 0};
    const LenStrEntry* found = (const LenStrEntry*)hashmap_get(m, &probe);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 2);

    // different length but same prefix → miss
    probe.name = "alphax";
    probe.name_len = 6;
    EXPECT_EQ(hashmap_get(m, &probe), nullptr);

    // empty key edge case
    LenStrEntry empty{"", 0, 99};
    hashmap_set(m, &empty);
    LenStrEntry empty_probe{nullptr, 0, 0};
    found = (const LenStrEntry*)hashmap_get(m, &empty_probe);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 99);

    hashmap_free(m);
}

TEST(HashmapHelpersTest, Field2CompositeKey) {
    struct hashmap* m = field2_entry_new(0);
    ASSERT_NE(m, nullptr);

    const char tag_a[] = "same";
    const char tag_b[] = "same";
    Field2Entry a{7, tag_a, 10};
    Field2Entry b{7, tag_b, 20};
    Field2Entry c{8, tag_a, 30};
    hashmap_set(m, &a);
    hashmap_set(m, &b);
    hashmap_set(m, &c);
    EXPECT_EQ(hashmap_count(m), 3u);

    Field2Entry probe{7, tag_a, 0};
    const Field2Entry* found = (const Field2Entry*)hashmap_get(m, &probe);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 10);

    probe.tag = tag_b;
    found = (const Field2Entry*)hashmap_get(m, &probe);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 20);

    hashmap_free(m);
}

TEST(HashmapHelpersTest, Field3CompositeKey) {
    struct hashmap* m = field3_entry_new(0);
    ASSERT_NE(m, nullptr);

    const char tmpl[] = "tmpl";
    const char hover[] = "hover";
    const char focus[] = "focus";
    Field3Entry a{11, tmpl, hover, 1};
    Field3Entry b{11, tmpl, focus, 2};
    hashmap_set(m, &a);
    hashmap_set(m, &b);

    Field3Entry probe{11, tmpl, focus, 0};
    const Field3Entry* found = (const Field3Entry*)hashmap_get(m, &probe);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 2);

    probe.state = hover;
    found = (const Field3Entry*)hashmap_get(m, &probe);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 1);

    hashmap_free(m);
}

TEST(HashmapHelpersTest, NewWithFreeHookInvokedOnFree) {
    static int frees = 0;
    frees = 0;
    struct hashmap* m = owned_entry_new_with_free(0, [](void* item) {
        auto* e = (OwnedEntry*)item;
        (*e->counter)++;
    });
    OwnedEntry a; strcpy(a.name, "x"); a.counter = &frees;
    OwnedEntry b; strcpy(b.name, "y"); b.counter = &frees;
    hashmap_set(m, &a);
    hashmap_set(m, &b);
    hashmap_free(m);
    EXPECT_EQ(frees, 2);
}
