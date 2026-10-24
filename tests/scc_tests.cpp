#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "parallel_tarjan.hpp"
#include "ptarjan/scc.hpp"

namespace {

using ptarjan::Components;
using ptarjan::Edge;
using ptarjan::Graph;
using ptarjan::Vertex;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Exception, class Function>
void requireThrows(Function function, const std::string& message) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

// Independent iterative Kosaraju implementation: the test oracle does not
// share the production algorithms' low-link logic or cell representation.
Components expectedComponents(const Graph& graph) {
    const Vertex vertexCount = graph.vertexCount();
    std::vector<std::vector<Vertex>> reverse(vertexCount);
    for (Vertex vertex = 0; vertex < vertexCount; ++vertex) {
        for (const Vertex successor : graph.successors(vertex)) {
            reverse[successor].push_back(vertex);
        }
    }
    std::vector<bool> visited(vertexCount, false);
    std::vector<Vertex> finishingOrder;
    for (Vertex root = 0; root < vertexCount; ++root) {
        if (visited[root]) {
            continue;
        }
        std::vector<std::pair<Vertex, std::size_t>> stack{{root, 0}};
        visited[root] = true;
        while (!stack.empty()) {
            const auto vertex = stack.back().first;
            auto& nextSuccessor = stack.back().second;
            const auto successors = graph.successors(vertex);
            if (nextSuccessor == successors.size()) {
                finishingOrder.push_back(vertex);
                stack.pop_back();
                continue;
            }
            const auto successor = successors[nextSuccessor++];
            if (!visited[successor]) {
                visited[successor] = true;
                stack.emplace_back(successor, 0);
            }
        }
    }
    std::fill(visited.begin(), visited.end(), false);
    std::vector<Vertex> vertices;
    std::vector<std::size_t> offsets{0};
    for (auto root = finishingOrder.rbegin(); root != finishingOrder.rend(); ++root) {
        if (visited[*root]) {
            continue;
        }
        std::vector<Vertex> stack{*root};
        visited[*root] = true;
        while (!stack.empty()) {
            const auto vertex = stack.back();
            stack.pop_back();
            vertices.push_back(vertex);
            for (const auto predecessor : reverse[vertex]) {
                if (!visited[predecessor]) {
                    visited[predecessor] = true;
                    stack.push_back(predecessor);
                }
            }
        }
        offsets.push_back(vertices.size());
    }
    return Components(std::move(vertices), std::move(offsets)).normalized();
}

// A test-only barrier makes every worker compete for roots at the same time.
// Otherwise small graphs can finish before most worker threads have started.
class SynchronizedRoots final : public ptarjan::detail::VertexOrderRoots {
public:
    SynchronizedRoots(Vertex vertexCount, unsigned threadCount)
        : VertexOrderRoots(vertexCount, threadCount), expectedWorkers(threadCount),
          hasArrived(threadCount, false) {}

    bool nextBlock(ptarjan::detail::Worker& worker, ptarjan::detail::RootRange& range) override {
        {
            std::unique_lock<std::mutex> lock(startMutex);
            if (!hasArrived[worker.id()]) {
                hasArrived[worker.id()] = true;
                ++arrivedWorkers;
                if (arrivedWorkers == expectedWorkers) {
                    startCondition.notify_all();
                } else {
                    startCondition.wait(lock, [this] { return arrivedWorkers == expectedWorkers; });
                }
            }
        }
        return VertexOrderRoots::nextBlock(worker, range);
    }

private:
    const unsigned expectedWorkers;
    unsigned arrivedWorkers = 0;
    std::vector<bool> hasArrived;
    std::mutex startMutex;
    std::condition_variable startCondition;
};

// lockedQueue selects the mutex-backed pending queue that ThreadSanitizer can model.
Components runSynchronized(const Graph& graph, unsigned threadCount, bool lockedQueue = false) {
    std::unique_ptr<ptarjan::detail::PendingQueue> pending;
    if (lockedQueue) {
        pending = std::make_unique<ptarjan::detail::LockedPendingQueue>();
    } else {
        pending = std::make_unique<ptarjan::detail::LockFreePendingQueue>(threadCount);
    }
    SynchronizedRoots roots(graph.vertexCount(), threadCount);
    ptarjan::detail::ParallelTarjan algorithm(graph, threadCount, *pending, roots);
    return algorithm.run().normalized();
}

void requirePartition(const Components& components, const Graph& graph,
                      const std::string& description) {
    require(components.vertexCount() == graph.vertexCount(),
            description + ": result does not hold every vertex exactly once");
    require(components.normalized() == expectedComponents(graph),
            description + ": SCC partition differs from the oracle");
}

void compareAlgorithms(const Graph& graph, unsigned threadCount, const std::string& description) {
    requirePartition(ptarjan::sequentialScc(graph), graph, description + " (sequential)");
    requirePartition(ptarjan::parallelScc(graph, threadCount), graph,
                     description + " (" + std::to_string(threadCount) + " threads)");
    requirePartition(ptarjan::hybridScc(graph, threadCount), graph,
                     description + " (hybrid, " + std::to_string(threadCount) + " threads)");
}

Graph randomGraph(Vertex vertexCount, std::size_t edgeCount, std::mt19937& generator) {
    std::vector<Edge> edges;
    edges.reserve(edgeCount);
    for (std::size_t edge = 0; edge < edgeCount; ++edge) {
        edges.push_back({static_cast<Vertex>(generator() % vertexCount),
                         static_cast<Vertex>(generator() % vertexCount)});
    }
    return Graph(vertexCount, edges);
}

void apiTests() {
    const Graph empty;
    require(empty.vertexCount() == 0 && empty.edgeCount() == 0, "default graph is not empty");
    require(ptarjan::sequentialScc(empty).empty() && ptarjan::parallelScc(empty, 3).empty(),
            "empty graph has components");

    const std::vector<Edge> edges{{2, 0}, {0, 1}, {2, 3}, {0, 2}};
    const Graph graph(4, edges);
    require(graph.vertexCount() == 4 && graph.edgeCount() == 4,
            "edge-list graph has the wrong size");
    const auto fromZero = graph.successors(0);
    require(std::vector<Vertex>(fromZero.begin(), fromZero.end()) == std::vector<Vertex>{1, 2},
            "successors must keep their input order");
    require(graph.successors(1).empty() && graph.successors(3).empty(), "sinks have successors");
    require(Graph({{1, 2}, {}, {0, 3}, {}}).targets().size() == 4, "adjacency graph lost edges");

    requireThrows<std::out_of_range>([] { Graph(2, std::vector<Edge>{{0, 2}}); },
                                     "an edge to a missing vertex must be rejected");
    requireThrows<std::out_of_range>([] { Graph({{1}}); },
                                     "an adjacency list naming a missing vertex must be rejected");
    requireThrows<std::invalid_argument>([] { (void)ptarjan::parallelScc(Graph(), 0); },
                                         "zero workers must be rejected before starting threads");
    requireThrows<std::invalid_argument>([] { (void)ptarjan::hybridScc(Graph(), 0); },
                                         "hybrid must reject zero workers");
    require(ptarjan::hybridScc(empty, 4).empty(), "empty graph has hybrid components");
    require(ptarjan::defaultThreadCount() >= 1, "the default thread count must be positive");

    const Components components({4, 1, 3, 0, 2}, {0, 2, 3, 5});
    require(components.size() == 3 && components.vertexCount() == 5, "component counts are wrong");
    require(components[2].size() == 2 && components[2][0] == 0, "component view is wrong");
    std::size_t visited = 0;
    for (const auto component : components) {
        visited += component.size();
    }
    require(visited == 5, "iteration skipped vertices");
    require(components.normalized().toVectors() ==
                std::vector<std::vector<Vertex>>{{0, 2}, {1, 4}, {3}},
            "normalized() must sort components and their vertices");
    requireThrows<std::invalid_argument>([] { Components({1, 2}, {0, 3}); },
                                         "offsets past the vertex buffer must be rejected");
    requireThrows<std::invalid_argument>([] { Components({1, 2}, {0, 2, 1, 2}); },
                                         "decreasing offsets must be rejected");
}

void differentialTests() {
    compareAlgorithms(Graph(32, {}), 8, "isolated vertices");

    const std::vector<Edge> shapes{{1, 1}, {2, 3}, {3, 2}, {3, 4}, {4, 5}, {5, 6}, {6, 4}, {6, 4}};
    for (const auto threadCount : {1U, 2U, 8U, 32U}) {
        // Self-loops, a directed bridge, and a duplicate edge.
        compareAlgorithms(Graph(7, shapes), threadCount, "SCCs with directed bridges");
    }

    for (const Vertex length : {Vertex{2048}, Vertex{200000}}) {
        std::vector<Edge> chain;
        for (Vertex vertex = 1; vertex < length; ++vertex) {
            chain.push_back({vertex - 1, vertex});
        }
        compareAlgorithms(Graph(length, chain), 8, "chain of " + std::to_string(length));
        chain.push_back({length - 1, 0});
        compareAlgorithms(Graph(length, chain), 8, "cycle of " + std::to_string(length));
    }

    std::mt19937 generator(0x5CC2026U);
    for (unsigned graphNumber = 0; graphNumber < 60; ++graphNumber) {
        const auto vertexCount = static_cast<Vertex>(1 + generator() % 96);
        const std::size_t edgeCount = std::size_t{vertexCount} * (graphNumber % 7);
        compareAlgorithms(randomGraph(vertexCount, edgeCount, generator), 1 + graphNumber % 16,
                          "random graph " + std::to_string(graphNumber));
    }
    for (unsigned graphNumber = 0; graphNumber < 6; ++graphNumber) {
        compareAlgorithms(randomGraph(20000, 10000 + 6000 * graphNumber, generator), 8,
                          "large random graph " + std::to_string(graphNumber));
    }
}

void threadBoundaryTests() {
    // The scheduler used to keep one bit per worker in a 64-bit word. Worker
    // counts on and past that old boundary must terminate normally.
    for (const auto threadCount : {1U, 63U, 64U, 65U, 128U}) {
        require(runSynchronized(Graph(), threadCount).empty(),
                "empty graph returned components at thread-count boundary");
    }
    std::vector<Edge> cycle;
    for (Vertex vertex = 0; vertex < 256; ++vertex) {
        cycle.push_back({vertex, (vertex + 1) % 256});
    }
    const Graph cycleGraph(256, cycle);
    for (const auto threadCount : {64U, 65U, 128U}) {
        require(runSynchronized(cycleGraph, threadCount) == expectedComponents(cycleGraph),
                std::to_string(threadCount) + " workers produced an incorrect cycle partition");
    }
}

void contentionTests(bool lockedQueue = false) {
    std::mt19937 generator(0xC0111DEU);
    for (unsigned round = 0; round < 30; ++round) {
        constexpr Vertex vertexCount = 512;
        std::vector<Edge> edges;
        for (Vertex vertex = 0; vertex < vertexCount; ++vertex) {
            // Many small SCCs connected only in one direction, so that searches
            // collide, suspend, and form blocking cycles.
            const Vertex clusterStart = (vertex / 8) * 8;
            edges.push_back({vertex, round % 5 == 4 ? (vertex + 1) % vertexCount
                                                    : clusterStart + (vertex + 1) % 8});
            for (unsigned edge = 0; edge < 3; ++edge) {
                edges.push_back(
                    {vertex, vertex + static_cast<Vertex>(generator() % (vertexCount - vertex))});
            }
        }
        const Graph graph(vertexCount, edges);
        const unsigned threadCount = round % 2 == 0 ? 32U : 64U;
        std::cerr << "contention round=" << round << " workers=" << threadCount
                  << " queue=" << (lockedQueue ? "locked" : "moodycamel") << std::endl;
        require(runSynchronized(graph, threadCount, lockedQueue) == expectedComponents(graph),
                "contention round " + std::to_string(round) + " differs from the oracle");
    }
}

// Small sparse graphs exhaust the roots almost at once, so most workers spend
// the run idle while a few searches suspend and resume. That is the phase in
// which the old termination flags could hang.
void terminationTests() {
    std::mt19937 generator(0x7E2A1AU);
    for (unsigned round = 0; round < 60; ++round) {
        const Graph graph = randomGraph(1500, 1500, generator);
        const unsigned threadCount = std::vector<unsigned>{63, 96, 128}[round % 3];
        require(ptarjan::parallelScc(graph, threadCount).normalized() == expectedComponents(graph),
                "termination round " + std::to_string(round) + " differs from the oracle");
    }
}

// Many searches suspend on one hub while the hub's owner walks a long cycle,
// growing the hub's blocked list well past its embedded buffer.
void fanInTests() {
    constexpr Vertex leafCount = 20000;
    constexpr Vertex cycleLength = 200000;
    std::vector<Edge> edges;
    for (Vertex leaf = 1; leaf <= leafCount; ++leaf) {
        edges.push_back({leaf, 0});
    }
    Vertex previous = 0;
    for (Vertex vertex = leafCount + 1; vertex <= leafCount + cycleLength; ++vertex) {
        edges.push_back({previous, vertex});
        previous = vertex;
    }
    edges.push_back({previous, 0});
    const Graph graph(leafCount + cycleLength + 1, edges);
    for (const auto threadCount : {4U, 16U}) {
        require(runSynchronized(graph, threadCount) == expectedComponents(graph),
                "fan-in with " + std::to_string(threadCount) + " workers differs from the oracle");
    }
}

// Shapes that exercise each phase of the hybrid algorithm: graphs trimming finishes, a giant
// component with trees hanging off both sides, and several components of similar size
// so the forward-backward pivot takes only one of them. Many threads and repeats, since
// the trim queue and the barriers are concurrency code of their own.
void hybridTests() {
    std::mt19937 generator(0x3A57E9U);
    for (unsigned round = 0; round < 40; ++round) {
        const unsigned threadCount = std::vector<unsigned>{1, 2, 7, 16, 64}[round % 5];
        std::vector<Edge> edges;
        Vertex vertexCount = 0;
        switch (round % 4) {
        case 0: { // DAG: trimming removes everything
            vertexCount = 3000;
            for (Vertex v = 0; v + 1 < vertexCount; ++v)
                for (int k = 0; k < 3; ++k)
                    edges.push_back(
                        {v, v + 1 +
                                static_cast<Vertex>(generator() %
                                                    std::min<Vertex>(50, vertexCount - v - 1))});
            break;
        }
        case 1: { // giant component (a cycle with chords) plus in- and out-trees and self-loops
            vertexCount = 4000;
            for (Vertex v = 0; v < 2000; ++v) {
                edges.push_back({v, (v + 1) % 2000});
                edges.push_back({v, static_cast<Vertex>(generator() % 2000)});
            }
            for (Vertex v = 2000; v < 3000; ++v)
                edges.push_back({v, static_cast<Vertex>(generator() % v)}); // into the giant part
            for (Vertex v = 3000; v < vertexCount; ++v)
                edges.push_back({static_cast<Vertex>(generator() % 2000), v}); // out of it
            for (Vertex v = 0; v < vertexCount; v += 97)
                edges.push_back({v, v});
            break;
        }
        case 2: { // several equal cycles joined in one direction
            vertexCount = 4000;
            for (Vertex v = 0; v < vertexCount; ++v) {
                const Vertex start = v - v % 500;
                edges.push_back({v, start + (v + 1) % 500});
                if (start + 500 < vertexCount && generator() % 50 == 0)
                    edges.push_back({v, start + 500 + static_cast<Vertex>(generator() % 500)});
            }
            break;
        }
        default: // sparse random graph: many small components and trimmable vertices
            vertexCount = 3000;
            for (std::size_t e = 0; e < 3600; ++e)
                edges.push_back({static_cast<Vertex>(generator() % vertexCount),
                                 static_cast<Vertex>(generator() % vertexCount)});
        }
        const Graph graph(vertexCount, edges);
        requirePartition(ptarjan::hybridScc(graph, threadCount), graph,
                         "hybrid round " + std::to_string(round) + " with " +
                             std::to_string(threadCount) + " threads");
    }
}

} // namespace

int main(int argumentCount, char** arguments) {
    try {
        require(
            argumentCount == 3 && std::string(arguments[1]) == "--case",
            "usage: ptarjan_tests --case "
            "api|differential|thread-boundaries|contention|contention-locked|termination|fan-in|"
            "hybrid");
        const std::string testCase = arguments[2];
        if (testCase == "api") {
            apiTests();
        } else if (testCase == "differential") {
            differentialTests();
        } else if (testCase == "thread-boundaries") {
            threadBoundaryTests();
        } else if (testCase == "contention") {
            contentionTests();
        } else if (testCase == "contention-locked") {
            contentionTests(true);
        } else if (testCase == "termination") {
            terminationTests();
        } else if (testCase == "fan-in") {
            fanInTests();
        } else if (testCase == "hybrid") {
            hybridTests();
        } else {
            throw std::invalid_argument("unknown test case: " + testCase);
        }
        std::cout << "PASS " << testCase << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
