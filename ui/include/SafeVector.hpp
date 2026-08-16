#pragma once

#include <vector>
#include <functional>
#include <assert.h>
#include "gui/Widget.hpp"

template <typename T>
struct SafeVectorItem {
    T item;
    bool alive = false;
};

// A vector that can be modified while iterating
template <typename T>
class SafeVector {
public:
    using PointerType = typename std::pointer_traits<T>::element_type*;

    SafeVector() = default;
    SafeVector(const SafeVector&) = delete;
    SafeVector& operator=(const SafeVector&) = delete;
    ~SafeVector() {
        clear();
    }

    void push_back(T item) {
        data.push_back({std::move(item), true});
        ++num_items_alive;
    }

    // Safe to call when vector is empty
    // TODO: Make this iterator safe
    void pop_back() {
        if(!data.empty()) {
            gsr::add_widget_to_remove(std::move(data.back().item));
            data.pop_back();
            --num_items_alive;
        }
    }

    // Might not remove the data immediately if inside for_each loop.
    // In that case the item is removed at the end of the loop.
    void remove(PointerType item_to_remove) {
        if(for_each_depth == 0) {
            remove_item(item_to_remove);
            return;
        }

        SafeVectorItem<T> *item = get_item(item_to_remove);
        if(item && item->alive) {
            item->alive = false;
            --num_items_alive;
            has_items_to_remove = true;
        }
    }

    // Safe to call when vector is empty, in which case it returns nullptr
    T* back() {
        for(auto it = data.rbegin(), end = data.rend(); it != end; ++it) {
            if(it->alive)
                return &it->item;
        }
        return nullptr;
    }

    // TODO: Make this iterator safe
    void clear() {
        for(auto &item : data) {
            gsr::add_widget_to_remove(std::move(item.item));
        }
        data.clear();
        num_items_alive = 0;
    }

    // Return true from |callback| to continue. This function returns false if |callback| returned false
    bool for_each(std::function<bool(T &t)> callback, bool include_dead = false) {
        bool result = true;
        ++for_each_depth;

        for(size_t i = 0; i < data.size(); ++i) {
            if(data[i].alive || include_dead) {
                result = callback(data[i].item);
                if(!result)
                    break;
            }
        }

        --for_each_depth;
        if(for_each_depth == 0)
            remove_dead_items();

        return result;
    }

    // Return true from |callback| to continue. This function returns false if |callback| returned false
    bool for_each_reverse(std::function<bool(T &t)> callback, bool include_dead = false) {
        bool result = true;
        ++for_each_depth;

        int i = (int)data.size() - 1;
        while(true) {
            // pop_back can be called while iterating so handle that case
            if(i >= (int)data.size())
                i = (int)data.size() - 1;

            if(i < 0)
                break;

            if(data[i].alive || include_dead) {
                result = callback(data[i].item);
                if(!result)
                    break;
            }

            --i;
        }

        --for_each_depth;
        if(for_each_depth == 0)
            remove_dead_items();

        return result;
    }

    T& operator[](size_t index) {
        assert(index < data.size());
        return data[index].item;
    }

    const T& operator[](size_t index) const {
        assert(index < data.size());
        return data[index].item;
    }

    size_t size() const {
        return (size_t)num_items_alive;
    }

    bool empty() const {
        return num_items_alive == 0;
    }

    void replace_item(PointerType item_to_replace, T new_item) {
        SafeVectorItem<T> *item = get_item(item_to_replace);
        if(item->alive) {
            gsr::add_widget_to_remove(std::move(item->item));
            item->item = std::move(new_item);
        }
    }
private:
    void remove_item(PointerType item_to_remove) {
        for(auto it = data.begin(), end = data.end(); it != end; ++it) {
            if(&*(it->item) == item_to_remove) {
                gsr::add_widget_to_remove(std::move(it->item));
                data.erase(it);
                --num_items_alive;
                return;
            }
        }
    }

    SafeVectorItem<T>* get_item(PointerType item_to_remove) {
        for(auto &item : data) {
            if(&*(item.item) == item_to_remove)
                return &item;
        }
        return nullptr;
    }

    void remove_dead_items() {
        if(!has_items_to_remove)
            return;

        for(auto it = data.begin(); it != data.end();) {
            if(it->alive) {
                ++it;
            } else {
                gsr::add_widget_to_remove(std::move(it->item));
                it = data.erase(it);
            }
        }
        has_items_to_remove = false;
    }
private:
    std::vector<SafeVectorItem<T>> data;
    int for_each_depth = 0;
    int num_items_alive = 0;
    bool has_items_to_remove = false;
};