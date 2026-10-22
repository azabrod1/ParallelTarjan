#include "AdjacencyListGraph.h"
#include "tarjan.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

int main(int argumentCount, char** arguments) {
    try {
        if (argumentCount > 2)
            throw std::invalid_argument("Usage: parallel_tarjan_demo [workers: 1..64]");

        unsigned workerCount = 4;
        if (argumentCount == 2) {
            const std::string suppliedCount(arguments[1]);
            std::size_t consumedCharacters = 0;
            const auto parsedCount = std::stoul(suppliedCount, &consumedCharacters);
            if (consumedCharacters != suppliedCount.size() || parsedCount < 1 || parsedCount > 64)
                throw std::invalid_argument("Worker count must be between 1 and 64");
            workerCount = static_cast<unsigned>(parsedCount);
        }

        AdjacencyListGraph<Vid> graph;
        graph.bulkInsertEdges({{0, 1}, {1, 2}, {2, 0}, {2, 3}, {3, 4}, {4, 3}});
        graph.insertVertex(5);

        // The original API transfers ownership of both the SCC collection and
        // its component vectors to the caller. Keep that ownership explicit.
        auto releaseComponents = [](SCC_Set* components) {
            for (const auto* component : *components)
                delete component;
            delete components;
        };
        std::unique_ptr<SCC_Set, decltype(releaseComponents)> components(
            Tarjan::multiThreadedTarjan(graph, workerCount, Sharded_Locked), releaseComponents);

        std::vector<std::vector<Vid>> orderedComponents;
        for (const auto* component : *components) {
            orderedComponents.push_back(*component);
            std::sort(orderedComponents.back().begin(), orderedComponents.back().end());
        }
        std::sort(orderedComponents.begin(), orderedComponents.end());

        std::cout << "Strongly connected components (" << workerCount << " workers):\n";
        for (const auto& component : orderedComponents) {
            std::cout << "{";
            for (std::size_t vertexPosition = 0; vertexPosition < component.size(); ++vertexPosition) {
                if (vertexPosition != 0) std::cout << ", ";
                std::cout << component[vertexPosition];
            }
            std::cout << "}\n";
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
