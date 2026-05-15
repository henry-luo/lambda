#ifndef LAMBDA_ARRAYLIST_HPP
#define LAMBDA_ARRAYLIST_HPP

#include <stddef.h>
#include <new>

#include "log.h"
#include "mem.h"
#include "ownership.hpp"

namespace lam {

template<typename T>
struct IsOwnedPtr {
    static const bool value = false;
};

template<typename T, typename Domain>
struct IsOwnedPtr<OwnedPtr<T, Domain> > {
    static const bool value = true;
};

template<typename T>
struct IsBorrowedPtr {
    static const bool value = false;
};

template<typename T, typename Domain>
struct IsBorrowedPtr<BorrowedPtr<T, Domain> > {
    static const bool value = true;
};

template<typename T>
struct IsRawPointer {
    static const bool value = false;
};

template<typename T>
struct IsRawPointer<T*> {
    static const bool value = true;
};

// ArrayList owns its element storage. Pointer-like elements are borrowed by convention.
template<typename T>
class ArrayList {
    static_assert(!IsOwnedPtr<T>::value,
                  "ArrayList<T> cannot store OwnedPtr; use ArrayOwnedList<T, Domain>");

public:
    explicit ArrayList(MemCategory category = MEM_CAT_CONTAINER, size_t initial_capacity = 16)
        : data_(nullptr), count_(0), capacity_(0), category_(category) {
        if (initial_capacity > 0) {
            reserve(initial_capacity);
        }
    }

    ~ArrayList() {
        clear();
        mem_free(data_);
    }

    ArrayList(const ArrayList&) = delete;
    ArrayList& operator=(const ArrayList&) = delete;

    ArrayList(ArrayList&& other) noexcept
        : data_(other.data_), count_(other.count_), capacity_(other.capacity_),
          category_(other.category_) {
        other.data_ = nullptr;
        other.count_ = 0;
        other.capacity_ = 0;
    }

    ArrayList& operator=(ArrayList&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        clear();
        mem_free(data_);
        data_ = other.data_;
        count_ = other.count_;
        capacity_ = other.capacity_;
        category_ = other.category_;
        other.data_ = nullptr;
        other.count_ = 0;
        other.capacity_ = 0;
        return *this;
    }

    bool good() const { return data_ != nullptr || capacity_ == 0; }
    size_t size() const { return count_; }
    size_t length() const { return count_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return count_ == 0; }

    T* data() { return data_; }
    const T* data() const { return data_; }

    T& at(size_t index) {
        bounds_check(index);
        return data_[index];
    }

    const T& at(size_t index) const {
        bounds_check(index);
        return data_[index];
    }

    T& operator[](size_t index) { return at(index); }
    const T& operator[](size_t index) const { return at(index); }

    T* try_get(size_t index) {
        return index < count_ ? &data_[index] : nullptr;
    }

    const T* try_get(size_t index) const {
        return index < count_ ? &data_[index] : nullptr;
    }

    T& front() {
        return at(0);
    }

    const T& front() const {
        return at(0);
    }

    T& back() {
        return at(count_ == 0 ? 0 : count_ - 1);
    }

    const T& back() const {
        return at(count_ == 0 ? 0 : count_ - 1);
    }

    T* begin() { return data_; }
    T* end() { return data_ + count_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + count_; }

    bool reserve(size_t requested_capacity) {
        if (requested_capacity <= capacity_) {
            return true;
        }
        return reallocate(requested_capacity);
    }

    bool append(const T& value) {
        return insert(count_, value);
    }

    bool push_back(const T& value) {
        return append(value);
    }

    bool prepend(const T& value) {
        return insert(0, value);
    }

    bool insert(size_t index, const T& value) {
        if (index > count_) {
            log_error("arraylist_insert_oob: index=%zu count=%zu", index, count_);
            return false;
        }
        if (count_ == capacity_ && !grow()) {
            return false;
        }

        for (size_t i = count_; i > index; --i) {
            new (&data_[i]) T(data_[i - 1]);
            data_[i - 1].~T();
        }
        new (&data_[index]) T(value);
        ++count_;
        return true;
    }

    bool remove(size_t index) {
        return remove_range(index, 1);
    }

    bool remove_range(size_t index, size_t range_length) {
        if (range_length == 0) {
            return true;
        }
        if (index >= count_ || range_length > count_ - index) {
            log_error("arraylist_remove_oob: index=%zu length=%zu count=%zu",
                      index, range_length, count_);
            return false;
        }

        for (size_t i = index; i < index + range_length; ++i) {
            data_[i].~T();
        }
        for (size_t i = index + range_length; i < count_; ++i) {
            new (&data_[i - range_length]) T(data_[i]);
            data_[i].~T();
        }
        count_ -= range_length;
        return true;
    }

    void clear() {
        for (size_t i = 0; i < count_; ++i) {
            data_[i].~T();
        }
        count_ = 0;
    }

    template<typename Equal>
    long index_of(const T& value, Equal equal) const {
        for (size_t i = 0; i < count_; ++i) {
            if (equal(data_[i], value)) {
                return (long)i; // INT_CAST_OK: ArrayList API returns signed not-found sentinel.
            }
        }
        return -1;
    }

    template<typename Compare>
    void sort(Compare compare) {
        if (count_ > 1) {
            sort_range(0, count_, compare);
        }
    }

private:
    static const size_t DEFAULT_CAPACITY = 16;

    T* data_;
    size_t count_;
    size_t capacity_;
    MemCategory category_;

    void bounds_check(size_t index) const {
        if (index < count_) {
            return;
        }
        log_error("arraylist_access_oob: index=%zu count=%zu", index, count_);
        abort();
    }

    bool grow() {
        size_t next_capacity = capacity_ == 0 ? DEFAULT_CAPACITY : capacity_ * 2;
        if (next_capacity < capacity_) {
            log_error("arraylist_grow_overflow: capacity=%zu", capacity_);
            return false;
        }
        return reallocate(next_capacity);
    }

    bool reallocate(size_t new_capacity) {
        if (new_capacity < count_) {
            log_error("arraylist_reallocate_invalid: requested=%zu count=%zu",
                      new_capacity, count_);
            return false;
        }
        if (new_capacity > ((size_t)-1) / sizeof(T)) {
            log_error("arraylist_reallocate_overflow: requested=%zu item_size=%zu",
                      new_capacity, sizeof(T));
            return false;
        }

        T* new_data = (T*)mem_alloc(new_capacity * sizeof(T), category_);
        if (!new_data) {
            log_error("arraylist_alloc_failed: capacity=%zu item_size=%zu",
                      new_capacity, sizeof(T));
            return false;
        }

        for (size_t i = 0; i < count_; ++i) {
            new (&new_data[i]) T(data_[i]);
            data_[i].~T();
        }
        mem_free(data_);
        data_ = new_data;
        capacity_ = new_capacity;
        return true;
    }

    template<typename Compare>
    void sort_range(size_t start, size_t length, Compare compare) {
        if (length <= 1) {
            return;
        }

        size_t pivot_index = start + length - 1;
        size_t lower_count = 0;
        for (size_t i = start; i < pivot_index; ++i) {
            if (compare(data_[i], data_[pivot_index]) < 0) {
                swap_values(data_[i], data_[start + lower_count]);
                ++lower_count;
            }
        }

        swap_values(data_[pivot_index], data_[start + lower_count]);
        sort_range(start, lower_count, compare);
        sort_range(start + lower_count + 1, length - lower_count - 1, compare);
    }

    static void swap_values(T& left, T& right) {
        if (&left == &right) {
            return;
        }
        T tmp(left);
        left = right;
        right = tmp;
    }
};

// ArrayOwnedList owns the pointees it stores through domain-tagged OwnedPtr values.
template<typename T, typename Domain>
class ArrayOwnedList {
    static_assert(!IsRawPointer<T>::value,
                  "ArrayOwnedList<T, Domain> expects pointee type T, not T*");
    static_assert(!IsOwnedPtr<T>::value,
                  "ArrayOwnedList<T, Domain> stores OwnedPtr internally; use pointee type T");
    static_assert(!IsBorrowedPtr<T>::value,
                  "ArrayOwnedList<T, Domain> cannot own BorrowedPtr; use pointee type T");

public:
    typedef T value_type;
    typedef Domain domain;

    explicit ArrayOwnedList(MemCategory category = MEM_CAT_CONTAINER, size_t initial_capacity = 16)
        : data_(nullptr), count_(0), capacity_(0), category_(category) {
        if (initial_capacity > 0) {
            reserve(initial_capacity);
        }
    }

    ~ArrayOwnedList() {
        clear();
        mem_free(data_);
    }

    ArrayOwnedList(const ArrayOwnedList&) = delete;
    ArrayOwnedList& operator=(const ArrayOwnedList&) = delete;

    ArrayOwnedList(ArrayOwnedList&& other) noexcept
        : data_(other.data_), count_(other.count_), capacity_(other.capacity_),
          category_(other.category_) {
        other.data_ = nullptr;
        other.count_ = 0;
        other.capacity_ = 0;
    }

    ArrayOwnedList& operator=(ArrayOwnedList&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        clear();
        mem_free(data_);
        data_ = other.data_;
        count_ = other.count_;
        capacity_ = other.capacity_;
        category_ = other.category_;
        other.data_ = nullptr;
        other.count_ = 0;
        other.capacity_ = 0;
        return *this;
    }

    bool good() const { return data_ != nullptr || capacity_ == 0; }
    size_t size() const { return count_; }
    size_t length() const { return count_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return count_ == 0; }

    BorrowedPtr<T, Domain> at(size_t index) {
        bounds_check(index);
        return BorrowedPtr<T, Domain>(data_[index].get());
    }

    BorrowedPtr<const T, Domain> at(size_t index) const {
        bounds_check(index);
        return BorrowedPtr<const T, Domain>(data_[index].get());
    }

    BorrowedPtr<T, Domain> operator[](size_t index) { return at(index); }
    BorrowedPtr<const T, Domain> operator[](size_t index) const { return at(index); }

    BorrowedPtr<T, Domain> try_get(size_t index) {
        return index < count_ ? BorrowedPtr<T, Domain>(data_[index].get())
                              : BorrowedPtr<T, Domain>();
    }

    BorrowedPtr<const T, Domain> try_get(size_t index) const {
        return index < count_ ? BorrowedPtr<const T, Domain>(data_[index].get())
                              : BorrowedPtr<const T, Domain>();
    }

    BorrowedPtr<T, Domain> front() {
        return at(0);
    }

    BorrowedPtr<const T, Domain> front() const {
        return at(0);
    }

    BorrowedPtr<T, Domain> back() {
        return at(count_ == 0 ? 0 : count_ - 1);
    }

    BorrowedPtr<const T, Domain> back() const {
        return at(count_ == 0 ? 0 : count_ - 1);
    }

    bool reserve(size_t requested_capacity) {
        if (requested_capacity <= capacity_) {
            return true;
        }
        return reallocate(requested_capacity);
    }

    bool append(OwnedPtr<T, Domain>&& value) {
        return insert(count_, static_cast<OwnedPtr<T, Domain>&&>(value));
    }

    bool push_back(OwnedPtr<T, Domain>&& value) {
        return append(static_cast<OwnedPtr<T, Domain>&&>(value));
    }

    bool prepend(OwnedPtr<T, Domain>&& value) {
        return insert(0, static_cast<OwnedPtr<T, Domain>&&>(value));
    }

    bool insert(size_t index, OwnedPtr<T, Domain>&& value) {
        if (index > count_) {
            log_error("array_owned_list_insert_oob: index=%zu count=%zu", index, count_);
            return false;
        }
        if (count_ == capacity_ && !grow()) {
            return false;
        }

        for (size_t i = count_; i > index; --i) {
            new (&data_[i]) OwnedPtr<T, Domain>(static_cast<OwnedPtr<T, Domain>&&>(data_[i - 1]));
            data_[i - 1].~OwnedPtr<T, Domain>();
        }
        new (&data_[index]) OwnedPtr<T, Domain>(static_cast<OwnedPtr<T, Domain>&&>(value));
        ++count_;
        return true;
    }

    bool remove(size_t index) {
        if (index >= count_) {
            log_error("array_owned_list_remove_oob: index=%zu count=%zu", index, count_);
            return false;
        }
        OwnedPtr<T, Domain> owned = remove_owned(index);
        return true;
    }

    OwnedPtr<T, Domain> remove_owned(size_t index) {
        if (index >= count_) {
            log_error("array_owned_list_remove_oob: index=%zu count=%zu", index, count_);
            return OwnedPtr<T, Domain>();
        }

        OwnedPtr<T, Domain> removed(static_cast<OwnedPtr<T, Domain>&&>(data_[index]));
        data_[index].~OwnedPtr<T, Domain>();
        for (size_t i = index + 1; i < count_; ++i) {
            new (&data_[i - 1]) OwnedPtr<T, Domain>(static_cast<OwnedPtr<T, Domain>&&>(data_[i]));
            data_[i].~OwnedPtr<T, Domain>();
        }
        --count_;
        return removed;
    }

    void clear() {
        for (size_t i = 0; i < count_; ++i) {
            data_[i].~OwnedPtr<T, Domain>();
        }
        count_ = 0;
    }

private:
    static const size_t DEFAULT_CAPACITY = 16;

    OwnedPtr<T, Domain>* data_;
    size_t count_;
    size_t capacity_;
    MemCategory category_;

    void bounds_check(size_t index) const {
        if (index < count_) {
            return;
        }
        log_error("array_owned_list_access_oob: index=%zu count=%zu", index, count_);
        abort();
    }

    bool grow() {
        size_t next_capacity = capacity_ == 0 ? DEFAULT_CAPACITY : capacity_ * 2;
        if (next_capacity < capacity_) {
            log_error("array_owned_list_grow_overflow: capacity=%zu", capacity_);
            return false;
        }
        return reallocate(next_capacity);
    }

    bool reallocate(size_t new_capacity) {
        if (new_capacity < count_) {
            log_error("array_owned_list_reallocate_invalid: requested=%zu count=%zu",
                      new_capacity, count_);
            return false;
        }
        if (new_capacity > ((size_t)-1) / sizeof(OwnedPtr<T, Domain>)) {
            log_error("array_owned_list_reallocate_overflow: requested=%zu item_size=%zu",
                      new_capacity, sizeof(OwnedPtr<T, Domain>));
            return false;
        }

        OwnedPtr<T, Domain>* new_data =
            (OwnedPtr<T, Domain>*)mem_alloc(new_capacity * sizeof(OwnedPtr<T, Domain>), category_);
        if (!new_data) {
            log_error("array_owned_list_alloc_failed: capacity=%zu item_size=%zu",
                      new_capacity, sizeof(OwnedPtr<T, Domain>));
            return false;
        }

        for (size_t i = 0; i < count_; ++i) {
            new (&new_data[i]) OwnedPtr<T, Domain>(static_cast<OwnedPtr<T, Domain>&&>(data_[i]));
            data_[i].~OwnedPtr<T, Domain>();
        }
        mem_free(data_);
        data_ = new_data;
        capacity_ = new_capacity;
        return true;
    }
};

template<typename List, typename StorageDomain>
class PersistentList {
    static_assert(DomainOutlives<typename List::domain, StorageDomain>::value,
                  "PersistentList: list pointee domain does not outlive storage domain");

public:
    typedef List list_type;
    typedef StorageDomain storage_domain;

    template<typename... Args>
    explicit PersistentList(Args&&... args)
        : list_(static_cast<Args&&>(args)...) {
    }

    List& get() { return list_; }
    const List& get() const { return list_; }

    List* operator->() { return &list_; }
    const List* operator->() const { return &list_; }

private:
    List list_;
};

} // namespace lam

#endif // LAMBDA_ARRAYLIST_HPP
