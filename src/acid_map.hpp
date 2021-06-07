#pragma once

#include "map_node.hpp"
#include "map_iterator.hpp"

#include <tuple>
#include <ostream>
#include <iterator>

namespace polyndrom {

template <class Key, class T, class Compare = std::less<Key>, class Allocator = std::allocator<std::pair<const Key, T>>>
class acid_map {
private:
    friend map_iterator<acid_map<Key, T, Compare, Allocator>>;
    template <class Map>
    friend class map_iterator;
    template <class Tree>
    friend class tree_verifier;
    using self_type = acid_map<Key, T, Compare, Allocator>;
    using node_ptr = node_pointer<std::pair<const Key, T>, Allocator>;
    using node_allocator_type = typename node_ptr::allocator_type;
public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::pair<const Key, T>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using key_compare = Compare;
    using allocator_type = Allocator;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = typename std::allocator_traits<Allocator>::pointer;
    using const_pointer = typename std::allocator_traits<Allocator>::const_pointer;
    using iterator = map_iterator<self_type>;
    acid_map(const allocator_type& allocator = allocator_type()) : node_allocator_(allocator) {}
    template <class K>
    iterator find(const K& key) {
        auto [parent, node] = find_node(root_, key);
        if (node == nullptr) {
            return end();
        }
        return iterator(node);
    }
    template <typename K>
    mapped_type& operator[](K&& key) {
        return try_emplace(std::forward<K>(key)).first->second;
    }
    mapped_type& at(const key_type& key) {
        auto [parent, node] = find_node(root_, key);
        if (node == nullptr) {
            throw std::out_of_range("Key does not exists");
        }
        return node->value_.second;
    }
    template <class K>
    bool contains(const K& key) const {
        return count(key) == 1;
    }
    template <class K>
    size_type count(const K& key) const {
        auto [parent, node] = find_node(root_, key);
        return static_cast<size_type>(node != nullptr);
    }
    template <class V>
    std::pair<iterator, bool> insert(V&& value) {
        const key_type& key = value.first;
        auto [parent, existing_node] = find_node(root_, key);
        if (existing_node != nullptr) {
            return std::make_pair(iterator(existing_node), false);
        }
        node_ptr node = node_ptr::construct_node(node_allocator_, std::forward<V>(value));
        insert_node(parent, node);
        return std::make_pair(iterator(node), true);
    }
    template <class ...Args>
    std::pair<iterator, bool> emplace(Args&& ...args) {
        node_ptr node = node_ptr::construct_node(node_allocator_, std::forward<Args>(args)...);
        auto [parent, existing_node] = find_node(root_, node->key());
        if (existing_node != nullptr) {
            node.destroy();
            return std::make_pair(iterator(existing_node), false);
        }
        insert_node(parent, node);
        return std::make_pair(iterator(node), true);
    }
    template <class K, class ...Args>
    std::pair<iterator, bool> try_emplace(K&& key, Args&& ...args) {
        auto [parent, existing_node] = find_node(root_, key);
        if (existing_node != nullptr) {
            return std::make_pair(iterator(existing_node), false);
        }
        node_ptr node = node_ptr::construct_node(node_allocator_, std::piecewise_construct,
                                       std::forward_as_tuple(std::forward<K>(key)),
                                       std::forward_as_tuple(std::forward<Args>(args)...));
        insert_node(parent, node);
        return std::make_pair(iterator(node), true);
    }
    size_type erase(const key_type& key) {
        auto [parent, node] = find_node(root_, key);
        if (node == nullptr) {
            return 0;
        }
        erase_node(node);
        return 1;
    }
    iterator erase(iterator pos) {
        node_ptr next = pos.node_.next();
        erase_node(pos.node_);
        return iterator(next);
    }
    iterator begin() {
        if (root_ == nullptr) {
            return end();
        }
        return iterator(root_.min());
    }
    iterator end() {
        return iterator();
    }
    size_type size() const {
        return size_;
    }
    bool empty() const {
        return size_ == 0;
    }
    void clear() {
        auto it1 = begin();
        while (it1 != end()) {
            auto it2 = it1++;
            erase(it2);
        }
        root_ = nullptr;
    }
    ~acid_map() {
        root_.force_destroy();
    }
private:
    void insert_node(node_ptr where, node_ptr node) {
        ++size_;
        if (root_ == nullptr) {
            root_ = node;
            return;
        }
        auto [parent, _] = find_node(where, node->key());
        node->parent_ = parent;
        if (is_less(node->key(), parent->key())) {
            parent->left_ = node;
        } else {
            parent->right_ = node;
        }
        update_height(node->parent_);
        rebalance_path(node->parent_);
    }
    void erase_node(node_ptr node) {
        if (node == nullptr || node->is_deleted_) {
            return;
        }
        node_ptr parent = node->parent_;
        node_ptr replacement;
        node_ptr for_rebalance;
        if (node->left_ == nullptr || node->right_ == nullptr) {
            if (node->left_ != nullptr) {
                replacement = node->left_;
            } else {
                replacement = node->right_;
            }
            if (replacement != nullptr) {
                replacement->parent_ = parent;
            }
            update_at_parent(parent, node, replacement);
            for_rebalance = parent;
        } else {
            replacement = node->right_.min();
            node_ptr replacement_parent = replacement->parent_;
            replacement->left_ = node->left_;
            if (node->left_ != nullptr) {
                node->left_->parent_ = replacement;
            }
            update_at_parent(parent, node, replacement);
            for_rebalance = replacement;
            if (node->right_ != replacement) {
                if (replacement->right_ != nullptr) {
                    replacement->right_->parent_ = replacement->parent_;
                }
                replacement_parent->left_ = replacement->right_;
                replacement->right_ = node->right_;
                node->right_->parent_ = replacement;
                for_rebalance = replacement_parent;
            }
            replacement->parent_ = parent;
        }
        node.make_deleted();
        if (node == root_) {
            root_ = replacement;
        }
        --size_;
        update_height(for_rebalance);
        rebalance_path(for_rebalance);
    }
    template <class K1, class K2>
    inline bool is_less(const K1& lhs, const K2& rhs) const {
        return comparator_(lhs, rhs);
    }
    template <class K1, class K2>
    inline bool is_equal(const K1& lhs, const K2& rhs) const {
        return !is_less(lhs, rhs) && !is_less(rhs, lhs);
    }
    template <class K>
    std::pair<node_ptr, node_ptr> find_node(node_ptr root, const K& key) const {
        node_ptr parent = nullptr;
        node_ptr node = root;
        while (true) {
            if (node == nullptr) {
                return std::make_pair(parent, node);
            }
            if (is_equal(node->key(), key)) {
                return std::make_pair(parent, node);
            }
            parent = node;
            if (is_less(key, node->key())) {
                node = node->left_;
            } else {
                node = node->right_;
            }
        }
    }
    void update_at_parent(node_ptr parent, node_ptr old_node, node_ptr new_node) const {
        if (parent == nullptr) {
            return;
        }
        if (old_node.is_left_child()) {
            parent->left_ = new_node;
        } else {
            parent->right_ = new_node;
        }
    }
    node_ptr rotate_left(node_ptr node) {
        node_ptr right_child = node->right_;
        if (node->right_ != nullptr) {
            node->right_ = right_child->left_;
        }
        if (right_child->left_ != nullptr) {
            right_child->left_->parent_ = node;
        }
        right_child->left_ = node;
        right_child->parent_ = node->parent_;
        node->parent_ = right_child;
        update_height(node);
        update_height(right_child);
        return right_child;
    }
    node_ptr rotate_right(node_ptr node) {
        node_ptr left_child = node->left_;
        if (node->left_ != nullptr) {
            node->left_ = left_child->right_;
        }
        if (left_child->right_ != nullptr) {
            left_child->right_->parent_ = node;
        }
        left_child->right_ = node;
        left_child->parent_ = node->parent_;
        node->parent_ = left_child;
        update_height(node);
        update_height(left_child);
        return left_child;
    }
    int height(node_ptr node) const {
        if (node == nullptr) {
            return 0;
        }
        return node->height_;
    }
    void update_height(node_ptr node) {
        if (node != nullptr) {
            node->height_ = std::max(height(node->left_), height(node->right_)) + 1;
        }
    }
    int balance_factor(node_ptr node) const {
        if (node == nullptr) {
            return 0;
        }
        return height(node->left_) - height(node->right_);
    }
    node_ptr rebalance(node_ptr node) {
        int bf = balance_factor(node);
        if (bf == 2) {
            if (balance_factor(node->left_) == -1) {
                node->left_ = rotate_left(node->left_);
            }
            node = rotate_right(node);
        } else if (bf == -2) {
            if (balance_factor(node->right_) == 1) {
                node->right_ = rotate_right(node->right_);
            }
            node = rotate_left(node);
        }
        update_height(node);
        return node;
    }
    node_ptr rebalance_path(node_ptr node) {
        if (node == nullptr) {
            return nullptr;
        }
        while (node != root_) {
            bool pos = true;
            if (node.is_right_child()) {
                pos = false;
            }
            node = rebalance(node);
            if (pos) {
                node->parent_->left_ = node;
            } else {
                node->parent_->right_ = node;
            }
            node = node->parent_;
        }
        root_ = rebalance(root_);
        return node;
    }
    node_ptr root_ = nullptr;
    size_type size_ = 0;
    key_compare comparator_;
    mutable node_allocator_type node_allocator_;
};

} // polyndrom