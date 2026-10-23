//
//  sequential_tarjan.cpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/28/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include "ptarjan/scc.hpp"

//   **** Iterative Single threaded Tarjan's algorithm implementation

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

    std::vector<Vertex> sccVertices;
    sccVertices.reserve(vertexCount);
    std::vector<std::size_t> sccOffsets{0};

    auto conquer = [&](Vertex vertex) {
        index[vertex] = rank[vertex] = cellCount++;
        tarjanStack.push_back(vertex);
        controlStack.emplace_back(vertex, offsets[vertex]);
    };

    for (Vertex root = 0; root < vertexCount; ++root) {
        if (index[root] != UNVISITED)
            continue;
        conquer(root); // Add the root of the search to the stack

        while (!controlStack.empty()) {
            const auto [curr, nextEdge] = controlStack.back();

            // Curr might have more neighbors not yet assigned to an SCC
            if (nextEdge != offsets[curr + 1]) {
                ++controlStack.back().second;
                const Vertex child = targets[nextEdge];

                if (index[child] == UNVISITED) // Not yet on stack nor assigned to an SCC
                    conquer(child);
                else if (index[child] != COMPLETE) // On the stack
                    rank[curr] = std::min(rank[curr], index[child]);
                continue;
            }

            // curr has no more neighbors left to explore
            controlStack.pop_back();

            // Update rank of the node that linked to curr
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
                    sccVertices.push_back(member);
                } while (member != curr);
                sccOffsets.push_back(sccVertices.size());
            }
        }
    }

    return {std::move(sccVertices), std::move(sccOffsets)};
}

} // namespace ptarjan
