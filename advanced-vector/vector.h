#pragma once

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

template<typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
            : buffer_(Allocate(capacity)), capacity_(capacity) {}

    ~RawMemory() { Deallocate(buffer_); }

    RawMemory(const RawMemory &) = delete;

    RawMemory &operator=(const RawMemory &rhs) = delete;

    RawMemory(RawMemory &&other) noexcept { Swap(other); }

    RawMemory &operator=(RawMemory &&rhs) noexcept {
        if (this != &rhs) {
            Deallocate(buffer_);
            Swap(rhs);
        }
        return *this;
    }

    T *operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним
        // элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T *operator+(size_t offset) const noexcept {
        return const_cast<RawMemory &>(*this) + offset;
    }

    const T &operator[](size_t index) const noexcept {
        return const_cast<RawMemory &>(*this)[index];
    }

    T &operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory &other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T *GetAddress() const noexcept { return buffer_; }

    T *GetAddress() noexcept { return buffer_; }

    size_t Capacity() const { return capacity_; }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T *Allocate(size_t n) {
        return n != 0 ? static_cast<T *>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T *buf) noexcept { operator delete(buf); }

    T *buffer_ = nullptr;
    size_t capacity_ = 0;
};

template<typename T>
class Vector {
public:
    Vector() = default;

    explicit Vector(size_t size) : data_(size), size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    ~Vector() { std::destroy_n(data_.GetAddress(), size_); }

    Vector(const Vector &other) : data_(other.size_), size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_,
                                  data_.GetAddress());
    }

    Vector(Vector &&other) noexcept { Swap(other); }

    Vector &operator=(const Vector &rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            } else {
                if (rhs.size_ >= size_) {
                    std::copy_n(rhs.data_.GetAddress(), size_, data_.GetAddress());
                    size_t offset = rhs.size_ - size_;
                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, offset,
                                              data_.GetAddress() + size_);
                } else {
                    std::copy_n(rhs.data_.GetAddress(), rhs.size_, data_.GetAddress());
                    size_t offset = size_ - rhs.size_;
                    std::destroy_n(data_.GetAddress() + rhs.size_, offset);
                }
                size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector &operator=(Vector &&rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }

    void Swap(Vector &other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        MoveOrCopyDataAndReplace(RawMemory<T>(new_capacity));
    }

    void Resize(size_t new_size) {
        if (new_size <= size_) {
            size_t offset = size_ - new_size;
            std::destroy_n(data_.GetAddress() + new_size, offset);
        } else {
            Reserve(new_size);
            size_t offset = new_size - size_;
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, offset);
        }
        size_ = new_size;
    }

    void PushBack(const T &value) { EmplaceBack(value); }

    void PushBack(T &&value) { EmplaceBack(std::move(value)); }

    template<typename... Args>
    T &EmplaceBack(Args &&...args) {
        if (size_ == Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);

            new(new_data + size_) T(std::forward<Args>(args)...);
            MoveOrCopyDataAndReplace(std::move(new_data));
        } else {
            new(data_ + size_) T(std::forward<Args>(args)...);
        }
        return *(data_ + size_++);
    }

    void PopBack() noexcept {
        assert(size_ > 0);
        std::destroy_n(data_.GetAddress() + --size_, 1);
    }

    using iterator = T *;
    using const_iterator = const T *;

    iterator begin() noexcept { return data_.GetAddress(); }

    iterator end() noexcept { return data_.GetAddress() + size_; }

    const_iterator begin() const noexcept { return data_.GetAddress(); }

    const_iterator end() const noexcept { return data_.GetAddress() + size_; }

    const_iterator cbegin() const noexcept { return begin(); }

    const_iterator cend() const noexcept { return end(); }

    iterator Insert(const_iterator pos, const T &value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T &&value) {
        return Emplace(pos, std::move(value));
    }

    template<typename... Args>
    iterator Emplace(const_iterator pos, Args &&...args) {
        if (pos == cend()) {
            return &EmplaceBack(std::forward<Args>(args)...);
        }

        size_t count = std::distance(cbegin(), pos);
        if (size_ == Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);

            new(new_data + count) T(std::forward<Args>(args)...);

            MoveOrCopyDataByPartAndReplace(std::move(new_data), count);
        } else {
            new(end()) T(std::forward<T>(*(end() - 1)));
            std::move_backward(begin() + count, end() - 1, end());
            *(begin() + count) = T(std::forward<Args>(args)...);
        }
        ++size_;
        return begin() + count;
    }

    iterator Erase(const_iterator pos) {
        size_t count = std::distance(pos, cend());
        std::move(end() - count + 1, end(), end() - count);
        PopBack();
        return end() - count + 1;
    }

    size_t Size() const noexcept { return size_; }

    size_t Capacity() const noexcept { return data_.Capacity(); }

    const T &operator[](size_t index) const noexcept {
        return const_cast<Vector &>(*this)[index];
    }

    T &operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

private:
    void MoveOrCopyDataAndReplace(RawMemory<T> &&new_data) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        } else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }

        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    void MoveOrCopyDataByPartAndReplace(RawMemory<T> &&new_data, size_t count) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move(begin(), begin() + count, new_data.GetAddress());
            std::uninitialized_move(begin() + count, end(), new_data.GetAddress() + count + 1);
        } else {
            std::uninitialized_copy(begin(), begin() + count, new_data.GetAddress());
            std::uninitialized_copy(begin() + count, end(), new_data.GetAddress() + count + 1);
        }

        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    RawMemory<T> data_;
    size_t size_ = 0;
};