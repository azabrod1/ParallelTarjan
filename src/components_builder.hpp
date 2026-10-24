#ifndef PTARJAN_COMPONENTS_BUILDER_HPP
#define PTARJAN_COMPONENTS_BUILDER_HPP

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "ptarjan/components.hpp"

namespace ptarjan::detail {

// Collects components one vertex at a time, in the layout Components stores.
class ComponentsBuilder {
public:
    void reserve(std::size_t vertexCount) { vertices.reserve(vertexCount); }

    // Adds vertices to the component being built.
    void add(Vertex vertex) { vertices.push_back(vertex); }
    void add(std::span<const Vertex> part) {
        vertices.insert(vertices.end(), part.begin(), part.end());
    }

    // Ends the component being built; does nothing if no vertex was added to it.
    void endComponent() {
        if (vertices.size() != offsets.back())
            offsets.push_back(vertices.size());
    }

    // Without endComponent's check: on a DAG every vertex comes through here.
    void addSingleton(Vertex vertex) {
        vertices.push_back(vertex);
        offsets.push_back(vertices.size());
    }

    // Appends every finished component of other.
    void append(const ComponentsBuilder& other) {
        const std::size_t base = vertices.size();
        vertices.insert(vertices.end(), other.vertices.begin(),
                        other.vertices.begin() + static_cast<std::ptrdiff_t>(other.offsets.back()));
        // Sized once rather than grown per component: results can hold millions of them.
        const std::size_t first = offsets.size();
        offsets.resize(first + other.offsets.size() - 1);
        for (std::size_t i = 1; i < other.offsets.size(); ++i)
            offsets[first + i - 1] = base + other.offsets[i];
    }

    void append(const Components& other) {
        for (const auto component : other) {
            add(component);
            endComponent();
        }
    }

    [[nodiscard]] Components build() && {
        endComponent();
        return {std::move(vertices), std::move(offsets)};
    }

private:
    std::vector<Vertex> vertices;
    std::vector<std::size_t> offsets{0};
};

} // namespace ptarjan::detail

#endif
