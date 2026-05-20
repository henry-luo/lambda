// test/lib/test_hashmap_helpers_gtest.cpp - tests for lib/hashmap_helpers
#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "../../lib/hashmap_helpers.h"
}

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

TEST(HashmapHelpersTest, OffsetBasedCstrAt) {
    struct InlineKey { char name[16]; int v; };
    InlineKey a; strcpy(a.name, "foo"); a.v = 1;
    InlineKey b; strcpy(b.name, "foo"); b.v = 2;
    InlineKey c; strcpy(c.name, "bar"); c.v = 3;
    size_t off = offsetof(InlineKey, name);
    EXPECT_EQ(hashmap_cmp_cstr_at(&a, &b, off), 0);
    EXPECT_NE(hashmap_cmp_cstr_at(&a, &c, off), 0);
    EXPECT_EQ(hashmap_hash_cstr_at(&a, off, 0, 0),
              hashmap_hash_cstr_at(&b, off, 0, 0));
    EXPECT_NE(hashmap_hash_cstr_at(&a, off, 0, 0),
              hashmap_hash_cstr_at(&c, off, 0, 0));
}

TEST(HashmapHelpersTest, OffsetBasedCstrPtrAt) {
    struct PtrKey { const char* name; int v; };
    PtrKey a{"foo", 1};
    char buf[8] = "foo";
    PtrKey b{buf, 2};
    PtrKey c{"bar", 3};
    size_t off = offsetof(PtrKey, name);
    EXPECT_EQ(hashmap_cmp_cstrptr_at(&a, &b, off), 0);
    EXPECT_NE(hashmap_cmp_cstrptr_at(&a, &c, off), 0);
    EXPECT_EQ(hashmap_hash_cstrptr_at(&a, off, 0, 0),
              hashmap_hash_cstrptr_at(&b, off, 0, 0));
}

TEST(HashmapHelpersTest, OffsetBasedIntAndPtr) {
    struct IntKey { int v; };
    IntKey a{5}, b{5}, c{7};
    size_t off = offsetof(IntKey, v);
    EXPECT_EQ(hashmap_cmp_int_at(&a, &b, off), 0);
    EXPECT_LT(hashmap_cmp_int_at(&a, &c, off), 0);
    EXPECT_GT(hashmap_cmp_int_at(&c, &a, off), 0);

    struct PK { void* p; };
    int x = 0, y = 0;
    PK pa{&x}, pb{&x}, pc{&y};
    size_t poff = offsetof(PK, p);
    EXPECT_EQ(hashmap_cmp_ptr_at(&pa, &pb, poff), 0);
    EXPECT_NE(hashmap_cmp_ptr_at(&pa, &pc, poff), 0);
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
