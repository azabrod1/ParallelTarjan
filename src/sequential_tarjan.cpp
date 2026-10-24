#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include "components_builder.hpp"
#include "ptarjan/scc.hpp"

namespace ptarjan {

Components sequentialScc(const Graph& graph) {
    const Vertex vertexCount = graph.vertexCount();
    const auto offsets = graph.offsets();
    const auto targets = graph.targets();

    // index[v] doubles as the vertex's state: unvisited, on the Tarjan stack (its
    // discovery index), or assigned to a component. Graph reserves the two markers.
    constexpr Vertex UNVISITED = std::numeric_limits<Vertex>::max();
    constexpr Vertex COMPLETE = UNVISITED - 1;
    std::vector<Vertex> index(vertexCount, UNVISITED);
    std::vector<Vertex> rank(vertexCount);

    std::vector<std::pair<Vertex, std::size_t>> controlStack; // vertex, next edge to follow
    std::vector<Vertex> tarjanStack;
    Vertex cellCount = 0;

    detail::ComponentsBuilder components;
    components.reserve(vertexCount);

    auto conquer = [&](Vertex vertex) {
        index[vertex] = rank[vertex] = cellCount++;
        tarjanStack.push_back(vertex);
        controlStack.emplace_back(vertex, offsets[vertex]);
    };

    for (Vertex root = 0; root < vertexCount; ++root) {
        if (index[root] != UNVISITED)
            continue;
        conquer(root);

        while (!controlStack.empty()) {
            const auto [curr, nextEdge] = controlStack.back();

            // Follow curr's next edge, if it has one left.
            if (nextEdge != offsets[curr + 1]) {
                ++controlStack.back().second;
                const Vertex child = targets[nextEdge];

                if (index[child] == UNVISITED)
                    conquer(child);
                else if (index[child] != COMPLETE) // On the stack
                    rank[curr] = std::min(rank[curr], index[child]);
                continue;
            }

            // Every edge of curr followed: backtrack.
            controlStack.pop_back();

            if (!controlStack.empty()) {
                const Vertex parent = controlStack.back().first;
                rank[parent] = std::min(rank[parent], rank[curr]);
            }

            if (rank[curr] == index[curr]) {
                Vertex member;
                do {
                    member = tarjanStack.back();
                    tarjanStack.pop_back();
                    index[member] = COMPLETE;
                    components.add(member);
                } while (member != curr);
                components.endComponent();
            }
        }
    }

    return std::move(components).build();
}

} // namespace ptarjan
