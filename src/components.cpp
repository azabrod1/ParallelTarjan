#include "ptarjan/components.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace ptarjan {

Components::Components(std::vector<Vertex> vertices, std::vector<std::size_t> offsets)
    : vertices_(std::move(vertices)), offsets_(std::move(offsets)) {
    if (offsets_.empty() || offsets_.front() != 0 || offsets_.back() != vertices_.size() ||
        !std::is_sorted(offsets_.begin(), offsets_.end()))
        throw std::invalid_argument("component offsets must rise from 0 to the vertex count");
}

Components Components::normalized() const {
    std::vector<Vertex> sorted(vertices_);
    for (std::size_t component = 0; component < size(); ++component)
        std::sort(sorted.begin() + offsets_[component], sorted.begin() + offsets_[component + 1]);

    // Components are disjoint, so ordering them by their smallest vertex is a
    // total order. Empty components, which the algorithms never produce, go first.
    std::vector<std::size_t> order(size());
    std::iota(order.begin(), order.end(), 0);
    auto smallest = [&](std::size_t component) {
        return offsets_[component] == offsets_[component + 1]
                   ? -1
                   : static_cast<long long>(sorted[offsets_[component]]);
    };
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        return smallest(left) < smallest(right);
    });

    std::vector<Vertex> vertices;
    vertices.reserve(vertices_.size());
    std::vector<std::size_t> offsets{0};
    offsets.reserve(offsets_.size());
    for (const std::size_t component : order) {
        vertices.insert(vertices.end(), sorted.begin() + offsets_[component],
                        sorted.begin() + offsets_[component + 1]);
        offsets.push_back(vertices.size());
    }
    return {std::move(vertices), std::move(offsets)};
}

std::vector<std::vector<Vertex>> Components::toVectors() const {
    std::vector<std::vector<Vertex>> components;
    components.reserve(size());
    for (const auto component : *this)
        components.emplace_back(component.begin(), component.end());
    return components;
}

} // namespace ptarjan
