#ifndef PTARJAN_COMPONENTS_HPP
#define PTARJAN_COMPONENTS_HPP

#include <cstddef>
#include <iterator>
#include <span>
#include <vector>

#include "ptarjan/graph.hpp"

namespace ptarjan {

/**
 * A partition of a graph's vertices into strongly connected components.
 *
 * All components share one vertex buffer, so a result costs two allocations
 * however many components there are. Component i is vertices [offsets[i],
 * offsets[i + 1]). The order of the components, and of the vertices within
 * each, is unspecified; normalized() gives a canonical order for comparisons.
 */
class Components {
public:
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::span<const Vertex>;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = std::span<const Vertex>;

        Iterator() = default;

        std::span<const Vertex> operator*() const { return (*components_)[position_]; }

        Iterator& operator++() {
            ++position_;
            return *this;
        }

        Iterator operator++(int) {
            Iterator previous = *this;
            ++position_;
            return previous;
        }

        bool operator==(const Iterator& other) const = default;

    private:
        friend class Components;
        Iterator(const Components* components, std::size_t position)
            : components_(components), position_(position) {}

        const Components* components_ = nullptr;
        std::size_t position_ = 0;
    };

    Components() = default;

    /// Takes the concatenated components and the offsets that delimit them.
    /// Throws std::invalid_argument unless offsets starts at 0, never decreases
    /// and ends at vertices.size().
    Components(std::vector<Vertex> vertices, std::vector<std::size_t> offsets);

    /// The number of components.
    std::size_t size() const noexcept { return offsets_.size() - 1; }

    bool empty() const noexcept { return size() == 0; }

    /// The total number of vertices in all components.
    std::size_t vertexCount() const noexcept { return vertices_.size(); }

    std::span<const Vertex> operator[](std::size_t component) const {
        return {vertices_.data() + offsets_[component], vertices_.data() + offsets_[component + 1]};
    }

    Iterator begin() const { return {this, 0}; }
    Iterator end() const { return {this, size()}; }

    /// The same partition with each component sorted and the components ordered by
    /// their smallest vertex. Two results describe the same partition exactly when
    /// their normalized forms are equal.
    [[nodiscard]] Components normalized() const;

    [[nodiscard]] std::vector<std::vector<Vertex>> toVectors() const;

    bool operator==(const Components& other) const = default;

private:
    std::vector<Vertex> vertices_;
    std::vector<std::size_t> offsets_{0};
};

} // namespace ptarjan

#endif
