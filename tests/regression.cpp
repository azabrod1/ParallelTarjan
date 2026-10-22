#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AdjacencyListGraph.h"
#include "tarjan.hpp"

namespace {

using Partition = std::vector<std::vector<Vid>>;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void normalize(Partition& partition) {
    for (auto& component : partition) {
        std::sort(component.begin(), component.end());
    }
    std::sort(partition.begin(), partition.end());
}

struct ResultDeleter {
    void operator()(SCC_Set* results) const {
        if (results != nullptr) {
            for (auto* component : *results) {
                delete component;
            }
            delete results;
        }
    }
};

Partition collect(SCC_Set* rawResults) {
    std::unique_ptr<SCC_Set, ResultDeleter> results(rawResults);
    Partition partition;
    for (const auto* component : *results) {
        partition.push_back(*component);
    }
    normalize(partition);
    return partition;
}

// Independent iterative Kosaraju implementation: the test oracle does not
// share the production algorithm's low-link logic or cell representation.
Partition expectedPartition(const Graph<Vid>& graph) {
    std::unique_ptr<std::unordered_set<Vid>> vertexSet(graph.getVertices());
    std::vector<Vid> vertices(vertexSet->begin(), vertexSet->end());
    std::sort(vertices.begin(), vertices.end());
    std::unordered_map<Vid, std::size_t> positions;
    for (std::size_t position = 0; position < vertices.size(); ++position) {
        positions.emplace(vertices[position], position);
    }
    std::vector<std::vector<std::size_t>> forward(vertices.size());
    std::vector<std::vector<std::size_t>> reverse(vertices.size());
    for (std::size_t position = 0; position < vertices.size(); ++position) {
        for (const Vid target : graph.getNeighborsVector(vertices[position])) {
            const auto targetPosition = positions.at(target);
            forward[position].push_back(targetPosition);
            reverse[targetPosition].push_back(position);
        }
    }
    std::vector<bool> visited(vertices.size(), false);
    std::vector<std::size_t> finishingOrder;
    for (std::size_t rootPosition = 0; rootPosition < vertices.size(); ++rootPosition) {
        if (visited[rootPosition]) {
            continue;
        }
        std::vector<std::pair<std::size_t, std::size_t>> stack{{rootPosition, 0}};
        visited[rootPosition] = true;
        while (!stack.empty()) {
            const auto position = stack.back().first;
            auto& nextNeighbor = stack.back().second;
            if (nextNeighbor == forward[position].size()) {
                finishingOrder.push_back(position);
                stack.pop_back();
                continue;
            }
            const auto targetPosition = forward[position][nextNeighbor++];
            if (!visited[targetPosition]) {
                visited[targetPosition] = true;
                stack.emplace_back(targetPosition, 0);
            }
        }
    }
    std::fill(visited.begin(), visited.end(), false);
    Partition partition;
    for (auto root = finishingOrder.rbegin(); root != finishingOrder.rend(); ++root) {
        if (visited[*root]) {
            continue;
        }
        std::vector<std::size_t> stack{*root};
        visited[*root] = true;
        std::vector<Vid> component;
        while (!stack.empty()) {
            const auto position = stack.back();
            stack.pop_back();
            component.push_back(vertices[position]);
            for (const auto predecessor : reverse[position]) {
                if (!visited[predecessor]) {
                    visited[predecessor] = true;
                    stack.push_back(predecessor);
                }
            }
        }
        partition.push_back(std::move(component));
    }
    normalize(partition);
    return partition;
}

// A test-only barrier makes every worker compete for roots at the same time.
// Otherwise small graphs can finish before most worker threads have started.
class SynchronizedRoots final : public UnrootedStealingQueue {
public:
    SynchronizedRoots(const Vid* vertices, Vid count,
                      Dictionary<Vid, WeakReference<Cell<Vid>>>& dictionary,
                      unsigned threadCount)
        : UnrootedStealingQueue(vertices, count, dictionary, threadCount),
          expectedWorkers(threadCount), hasArrived(threadCount, false) {}

    WeakReference<Cell<Vid>> next(Worker* worker) override {
        {
            std::unique_lock<std::mutex> lock(startMutex);
            if (!hasArrived[worker->getID()]) {
                hasArrived[worker->getID()] = true;
                ++arrivedWorkers;
                if (arrivedWorkers == expectedWorkers) {
                    startCondition.notify_all();
                } else {
                    startCondition.wait(lock, [this] {
                        return arrivedWorkers == expectedWorkers;
                    });
                }
            }
        }
        return UnrootedStealingQueue::next(worker);
    }

private:
    const unsigned expectedWorkers;
    unsigned arrivedWorkers = 0;
    std::vector<bool> hasArrived;
    std::mutex startMutex;
    std::condition_variable startCondition;
};

Partition runSynchronized(const Graph<Vid>& graph, unsigned threadCount, bool openAddressed,
                          bool lockedQueue = false) {
    std::unique_ptr<Dictionary<Vid, WeakReference<Cell<Vid>>>> dictionary;
    if (openAddressed) {
        // Force several resize boundaries, even for small test graphs.
        dictionary = std::make_unique<OpenAddressedMap>(2);
    } else {
        dictionary = std::make_unique<MutexDict<Vid, WeakReference<Cell<Vid>>>>();
    }
    std::unique_ptr<Pending> pending;
    if (lockedQueue) {
        pending = std::make_unique<LockedPendingQueue>();
    } else {
        pending = std::make_unique<LockFreePendingQueue>();
    }
    Vid vertexCount = 0;
    const Vid* vertices = graph.getVerticesArray(vertexCount);
    SynchronizedRoots roots(vertices, vertexCount, *dictionary, threadCount);
    MultiThreadedTarjan algorithm(graph, *dictionary, threadCount, *pending, roots);
    return collect(algorithm.run());
}

void compareAlgorithms(AdjacencyListGraph<Vid>& graph, unsigned threadCount,
                       DictType dictionary, const std::string& description) {
    graph.updateVertexArray();
    const auto expected = expectedPartition(graph);
    require(collect(Tarjan::singleThreadedTarjan(graph)) == expected,
            description + ": sequential SCC partition differs from oracle");
    require(collect(Tarjan::multiThreadedTarjan(graph, threadCount, dictionary)) == expected,
            description + ": parallel SCC partition differs from oracle");
}

void graphTests() {
    std::unordered_map<Vid, std::vector<Vid>> originalEdges{{10, {20}}, {20, {}}};
    AdjacencyListGraph<Vid> movedGraph(std::move(originalEdges));
    require(movedGraph.edgeExists(10, 20), "moved graph lost its edges");
    movedGraph.updateVertexArray();
    movedGraph.updateVertexArray();

    AdjacencyListGraph<Vid> graph;
    graph.insertEdge(1, 2);
    require(graph.hasVertex(2), "insertEdge must add the destination vertex");
    graph.insertEdge(1, 3);
    graph.removeEdge(1, 3); // First neighbor is not the target; used to loop forever.
    require(graph.edgeExists(1, 2) && !graph.edgeExists(1, 3), "removeEdge removed the wrong edge");
    graph.removeEdge(1, 99);
    graph.updateVertexArray();
    graph.insertVertex(4);
    Vid vertexCount = 0;
    graph.getVerticesArray(vertexCount);
    require(vertexCount == 4, "graph vertex cache is stale after insertion");
    {
        auto copy = graph;
        copy.updateVertexArray();
        require(copy.hasVertex(4), "copy lost graph data");
    }
    require(graph.hasVertex(4), "destroying a copy invalidated the original graph");
    compareAlgorithms(graph, 4, Mutex_Dict, "graph API mutations");

    AdjacencyListGraph<Vid> empty;
    std::unique_ptr<Vid[]> shuffled(empty.getVerticesArrayShuffled(vertexCount));
    require(vertexCount == 0, "empty shuffle must remain empty");
    compareAlgorithms(empty, 2, Mutex_Dict, "empty graph");
}

void differentialTests() {
    AdjacencyListGraph<Vid> isolated;
    for (Vid vertex = 0; vertex < 32; ++vertex) {
        isolated.insertVertex(vertex * 17);
    }
    compareAlgorithms(isolated, 8, Mutex_Dict, "isolated sparse vertex IDs");

    AdjacencyListGraph<Vid> shapes;
    shapes.insertVertex(0);
    shapes.insertEdge(1, 1);
    shapes.insertEdge(2, 3);
    shapes.insertEdge(3, 2);
    shapes.insertEdge(3, 4);
    shapes.insertEdge(4, 5);
    shapes.insertEdge(5, 6);
    shapes.insertEdge(6, 4);
    shapes.insertEdge(6, 4); // Duplicate edges do not duplicate component vertices.
    for (const auto threadCount : {1U, 2U, 8U, 32U}) {
        compareAlgorithms(shapes, threadCount, Mutex_Dict, "SCCs with directed bridges");
    }

    AdjacencyListGraph<Vid> longChain;
    for (Vid vertex = 0; vertex < 2048; ++vertex) {
        longChain.insertVertex(vertex);
        if (vertex != 0) {
            longChain.insertEdge(vertex - 1, vertex);
        }
    }
    compareAlgorithms(longChain, 8, OpenAddressed, "long chain");

    std::mt19937 generator(0x5CC2026U);
    for (unsigned graphNumber = 0; graphNumber < 30; ++graphNumber) {
        const Vid vertexCount = 1 + generator() % 96;
        AdjacencyListGraph<Vid> graph;
        for (Vid vertex = 0; vertex < vertexCount; ++vertex) {
            graph.insertVertex(vertex);
        }
        for (Vid source = 0; source < vertexCount; ++source) {
            for (unsigned edge = 0; edge < graphNumber % 7; ++edge) {
                graph.insertEdge(source, generator() % vertexCount);
            }
        }
        const std::string description = "random graph " + std::to_string(graphNumber);
        compareAlgorithms(graph, 1 + graphNumber % 16,
                          graphNumber % 2 == 0 ? Mutex_Dict : OpenAddressed, description);
    }
}

void threadBoundaryTests() {
    AdjacencyListGraph<Vid> empty;
    empty.updateVertexArray();
    bool rejected = false;
    try {
        collect(Tarjan::multiThreadedTarjan(empty, 0, Mutex_Dict));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "zero workers must be rejected before starting threads");
    // The scheduler used to keep one bit per worker in a 64-bit word. Worker
    // counts on and past that old boundary must terminate normally.
    for (const auto threadCount : {1U, 63U, 64U, 65U, 128U}) {
        require(runSynchronized(empty, threadCount, false).empty(),
                "empty graph returned components at thread-count boundary");
    }
    AdjacencyListGraph<Vid> cycle;
    for (Vid vertex = 0; vertex < 256; ++vertex) {
        cycle.insertEdge(vertex, (vertex + 1) % 256);
    }
    cycle.updateVertexArray();
    for (const auto threadCount : {64U, 65U, 128U}) {
        require(runSynchronized(cycle, threadCount, false) == expectedPartition(cycle),
                std::to_string(threadCount) + " workers produced an incorrect cycle partition");
    }
}

void contentionTests(bool isolateCore = false) {
    std::mt19937 generator(0xC0111DEU);
    for (unsigned round = 0; round < 30; ++round) {
        constexpr Vid vertexCount = 512;
        AdjacencyListGraph<Vid> graph;
        for (Vid vertex = 0; vertex < vertexCount; ++vertex) {
            graph.insertVertex(vertex);
            // Many small SCCs connected only in one direction increase cell
            // recycling while other workers still hold dictionary entries.
            const Vid clusterStart = (vertex / 8) * 8;
            graph.insertEdge(vertex, round % 5 == 4
                ? (vertex + 1) % vertexCount
                : clusterStart + (vertex + 1) % 8);
            for (unsigned edge = 0; edge < 3; ++edge) {
                graph.insertEdge(vertex, vertex + generator() % (vertexCount - vertex));
            }
        }
        graph.updateVertexArray();
        const bool openAddressed = !isolateCore && round % 3 == 0;
        std::cerr << "contention round=" << round
                  << " workers=" << (round % 2 == 0 ? 32U : 64U)
                  << " dictionary=" << (openAddressed ? "open-addressed" : "mutex")
                  << " queue=" << (isolateCore ? "locked" : "moodycamel")
                  << std::endl;
        require(runSynchronized(graph, round % 2 == 0 ? 32U : 64U, openAddressed, isolateCore) == expectedPartition(graph),
                "contention round " + std::to_string(round) + " differs from the oracle");
    }
}

// Small sparse graphs exhaust the roots almost at once, so most workers spend
// the run idle while a few searches suspend and resume. That is the phase in
// which the old termination flags could hang. Uses the default backend.
void terminationTests() {
    std::mt19937 generator(0x7E2A1AU);
    for (unsigned round = 0; round < 60; ++round) {
        constexpr Vid vertexCount = 1500;
        AdjacencyListGraph<Vid> graph;
        for (Vid vertex = 0; vertex < vertexCount; ++vertex) {
            graph.insertVertex(vertex);
        }
        for (Vid edge = 0; edge < vertexCount; ++edge) {
            graph.insertEdge(generator() % vertexCount, generator() % vertexCount);
        }
        graph.updateVertexArray();
        const unsigned threadCount = std::vector<unsigned>{63, 96, 128}[round % 3];
        require(collect(Tarjan::multiThreadedTarjan(graph, threadCount)) == expectedPartition(graph),
                "termination round " + std::to_string(round) + " differs from the oracle");
    }
}

} // namespace

int main(int argumentCount, char** arguments) {
    try {
        require(argumentCount == 3 && std::string(arguments[1]) == "--case",
                "usage: parallel_tarjan_tests --case graph|differential|thread-boundaries|contention|contention-locked|termination");
        const std::string testCase = arguments[2];
        if (testCase == "graph") {
            graphTests();
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
