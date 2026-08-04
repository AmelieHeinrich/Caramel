/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 20:51:02
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <cassert>

using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;
using float32 = float;
using float64 = double;

template<typename T>
using TUnique = std::unique_ptr<T>;

template<typename T, typename ...Args>
TUnique<T> MakeUnique(Args&&... args)
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}

template<typename T>
using TShared = std::shared_ptr<T>;

template<typename T, typename ...Args>
TShared<T> MakeShared(Args&&... args)
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}

template<typename T>
class TArray : public std::vector<T>
{
public:
    using std::vector<T>::vector;

    void Resize(size_t newSize) { this->resize(newSize); }
    void Resize(size_t newSize, const T& value) { this->resize(newSize, value); }
    void Reserve(size_t newCapacity) { this->reserve(newCapacity); }
    void Clear() { this->clear(); }
    void PushBack(const T& value) { this->push_back(value); }
    void PushBack(T&& value) { this->push_back(std::move(value)); }
    void PopBack() { this->pop_back(); }
    void Insert(size_t index, const T& value) { this->insert(this->begin() + index, value); }
    void Erase(size_t index) { this->erase(this->begin() + index); }

    typename std::vector<T>::iterator Begin() { return this->begin(); }
    typename std::vector<T>::iterator End() { return this->end(); }

    T* Data() { return this->data(); }
    const T* Data() const { return this->data(); }

    size_t Size() const { return this->size(); }
    size_t Capacity() const { return this->capacity(); }

    bool IsEmpty() const { return this->empty(); }
};

class String : public std::basic_string<char>
{
public:
    using std::basic_string<char>::basic_string;

    String(const std::basic_string<char>& other) : std::basic_string<char>(other) {}
    String(std::basic_string<char>&& other) : std::basic_string<char>(std::move(other)) {}

    void Resize(uint64 newSize) { this->resize(newSize); }
    void Reserve(uint64 newCapacity) { this->reserve(newCapacity); }
    void Clear() { this->clear(); }
    void PushBack(char value) { this->push_back(value); }
    void PopBack() { this->pop_back(); }

    bool Empty() const { return this->empty(); }

    uint64 Size() const { return this->size(); }
    uint64 Capacity() const { return this->capacity(); }

    const value_type* Data() { return this->data(); }
    const value_type* Data() const { return this->data(); }
    const value_type* CStr() const { return this->c_str(); }
};

namespace std
{
    template<>
    struct hash<String>
    {
        size_t operator()(const String& value) const noexcept
        {
            return std::hash<std::basic_string<char>>{}(value);
        }
    };
}

template<typename K, typename V>
class TDictionary : public std::unordered_map<K, V, std::hash<K>, std::equal_to<K>>
{
public:
    using std::unordered_map<K, V, std::hash<K>, std::equal_to<K>>::unordered_map;
    
    void Clear() { this->clear(); }
    void Insert(const K& key, const V& value) { this->insert({ key, value }); }
    void Erase(const K& key) { this->erase(key); }
    bool Contains(const K& key) const { return this->find(key) != this->end(); }
    
    typename std::unordered_map<K, V, std::hash<K>, std::equal_to<K>>::iterator Begin() { return this->begin(); }
    typename std::unordered_map<K, V, std::hash<K>, std::equal_to<K>>::iterator End() { return this->end(); }
    typename std::unordered_map<K, V, std::hash<K>, std::equal_to<K>>::const_iterator Begin() const { return this->begin(); }
    typename std::unordered_map<K, V, std::hash<K>, std::equal_to<K>>::const_iterator End() const { return this->end(); }
    typename std::unordered_map<K, V, std::hash<K>, std::equal_to<K>>::iterator Find(const K& key) { return this->find(key); }
    typename std::unordered_map<K, V, std::hash<K>, std::equal_to<K>>::const_iterator Find(const K& key) const { return this->find(key); }
    
    size_t Size() const { return this->size(); }
};

enum class EPlatform
{
    kWindows,
    kLinux,
    kMac,
    kPlaystation5,
    kNintendoSwitch2
};

inline EPlatform GetCurrentPlatform()
{
#if defined(CARAMEL_WINDOWS)
    return EPlatform::kWindows;
#elif defined(CARAMEL_LINUX)
    return EPlatform::kLinux;
#elif defined(CARAMEL_MACOS)
    return EPlatform::kMac;
#elif defined(CARAMEL_NDA_SONY)
    return EPlatform::kPlaystation5;
#elif defined(CARAMEL_NDA_NINTENDO)
    return EPlatform::kNintendoSwitch2;
#endif
}
