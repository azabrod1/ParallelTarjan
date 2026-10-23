#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ptarjan/scc.hpp"

int main(int argumentCount, char** arguments) {
    try {
        if (argumentCount > 2)
            throw std::invalid_argument("Usage: ptarjan_demo [workers >= 1]");

        unsigned workerCount = 4;
        if (argumentCount == 2) {
            const std::string suppliedCount(arguments[1]);
            std::size_t consumedCharacters = 0;
            const auto parsedCount = std::stoul(suppliedCount, &consumedCharacters);
            if (consumedCharacters != suppliedCount.size() || parsedCount < 1 || parsedCount > 4096)
                throw std::invalid_argument("Worker count must be between 1 and 4096");
            workerCount = static_cast<unsigned>(parsedCount);
        }

        // Vertices are numbered 0 .. 5. Vertex 5 has no edges.
        const std::vector<ptarjan::Edge> edges{{0, 1}, {1, 2}, {2, 0}, {2, 3}, {3, 4}, {4, 3}};
        const ptarjan::Graph graph(6, edges);

        // Component and vertex order is unspecified; normalize it for display.
        const ptarjan::Components components =
            ptarjan::parallelScc(graph, workerCount).normalized();

        std::cout << "Strongly connected components (" << workerCount << " workers):\n";
        for (const auto component : components) {
            std::cout << "{";
            for (std::size_t position = 0; position < component.size(); ++position) {
                if (position != 0)
                    std::cout << ", ";
                std::cout << component[position];
            }
            std::cout << "}\n";
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
