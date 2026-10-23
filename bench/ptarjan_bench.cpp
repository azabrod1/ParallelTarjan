// Times the sequential and parallel algorithms on a reproducible graph and checks
// that every parallel run finds the same partition as the sequential one.
//
//   ptarjan_bench --graph clusters --vertices 1000000 --threads 1,2,4,8 --runs 7

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/resource.h>

#include "graph_sources.hpp"
#include "ptarjan/scc.hpp"

namespace {

using namespace ptarjan;

struct Options {
    bench::GraphSpec graph;
    std::vector<unsigned> threads{1, 2, 4, 8};
    std::vector<std::string> algorithms{"parallel", "hybrid"};
    int runs = 5;
    int warmups = 1;
    bool verify = true;
    bool csv = false;
};

[[noreturn]] void usage(int exitCode) {
    std::cout
        << "usage: ptarjan_bench [options]\n"
           "  --graph FAMILY        graph to build (default random):\n"
        << bench::describeFamilies()
        << "  --vertices N          vertex count for synthetic graphs (default 1000000)\n"
           "  --degree D            average out-degree (default 4)\n"
           "  --cluster-size K      vertices per cluster for --graph clusters (default 64)\n"
           "  --seed S              generator seed (default 1)\n"
           "  --threads LIST        comma-separated worker counts (default 1,2,4,8)\n"
           "  --algorithms LIST     parallel, hybrid or both (default parallel,hybrid)\n"
           "  --runs R              timed runs per configuration (default 5)\n"
           "  --warmups W           untimed runs first (default 1)\n"
           "  --no-verify           skip comparing each parallel result with the sequential one\n"
           "  --csv                 print machine-readable rows\n";
    std::exit(exitCode);
}

unsigned long parseNumber(const std::string& text, const std::string& option) {
    std::size_t consumed = 0;
    const unsigned long value = std::stoul(text, &consumed);
    if (consumed != text.size())
        throw std::invalid_argument("bad value for " + option + ": " + text);
    return value;
}

Options parseOptions(int argumentCount, char** arguments) {
    Options options;
    for (int position = 1; position < argumentCount; ++position) {
        const std::string option = arguments[position];
        auto value = [&]() -> std::string {
            if (position + 1 >= argumentCount)
                throw std::invalid_argument(option + " needs a value");
            return arguments[++position];
        };
        if (option == "--help" || option == "-h") {
            usage(0);
        } else if (option == "--graph") {
            options.graph.family = value();
        } else if (option == "--vertices") {
            options.graph.vertices = static_cast<Vertex>(parseNumber(value(), option));
        } else if (option == "--degree") {
            options.graph.degree = std::stod(value());
        } else if (option == "--cluster-size") {
            options.graph.clusterSize = static_cast<Vertex>(parseNumber(value(), option));
        } else if (option == "--seed") {
            options.graph.seed = parseNumber(value(), option);
        } else if (option == "--algorithms") {
            options.algorithms.clear();
            const std::string list = value();
            for (std::size_t start = 0; start <= list.size();) {
                const std::size_t comma = std::min(list.find(',', start), list.size());
                const std::string name = list.substr(start, comma - start);
                if (name != "parallel" && name != "hybrid")
                    throw std::invalid_argument("unknown algorithm: " + name);
                options.algorithms.push_back(name);
                start = comma + 1;
            }
        } else if (option == "--threads") {
            options.threads.clear();
            const std::string list = value();
            for (std::size_t start = 0; start <= list.size();) {
                const std::size_t comma = std::min(list.find(',', start), list.size());
                options.threads.push_back(
                    static_cast<unsigned>(parseNumber(list.substr(start, comma - start), option)));
                start = comma + 1;
            }
        } else if (option == "--runs") {
            options.runs = static_cast<int>(parseNumber(value(), option));
        } else if (option == "--warmups") {
            options.warmups = static_cast<int>(parseNumber(value(), option));
        } else if (option == "--no-verify") {
            options.verify = false;
        } else if (option == "--csv") {
            options.csv = true;
        } else {
            std::cerr << "unknown option " << option << "\n";
            usage(2);
        }
    }
    if (options.runs < 1)
        throw std::invalid_argument("--runs must be at least 1");
    return options;
}

double millisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

struct Timing {
    double median, minimum, maximum;
};

template <class Algorithm>
Timing measure(const Options& options, Algorithm algorithm, const Components* expected) {
    for (int warmup = 0; warmup < options.warmups; ++warmup)
        algorithm();
    std::vector<double> times;
    for (int run = 0; run < options.runs; ++run) {
        const auto start = std::chrono::steady_clock::now();
        const Components result = algorithm();
        times.push_back(millisecondsSince(start));
        if (expected && result.normalized() != *expected)
            throw std::runtime_error("a parallel run disagreed with the sequential algorithm");
    }
    std::sort(times.begin(), times.end());
    return {times[times.size() / 2], times.front(), times.back()};
}

double peakResidentMegabytes() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0); // bytes
#else
    return static_cast<double>(usage.ru_maxrss) / 1024.0; // kilobytes
#endif
}

} // namespace

int main(int argumentCount, char** arguments) {
    try {
        const Options options = parseOptions(argumentCount, arguments);

        const auto buildStart = std::chrono::steady_clock::now();
        const Graph graph = bench::makeGraph(options.graph);
        const double buildTime = millisecondsSince(buildStart);

        const Components reference = sequentialScc(graph).normalized();
        std::size_t largest = 0;
        for (const auto component : reference)
            largest = std::max(largest, component.size());

        if (!options.csv) {
            std::printf("graph %s: %u vertices, %zu edges, %zu components (largest %zu), built in "
                        "%.0f ms\n",
                        options.graph.family.c_str(), graph.vertexCount(), graph.edgeCount(),
                        reference.size(), largest, buildTime);
            std::printf("%-10s %8s %10s %10s %10s %8s\n", "algorithm", "threads", "median ms",
                        "min ms", "max ms", "speedup");
        } else {
            std::printf(
                "family,vertices,edges,components,algorithm,threads,median_ms,min_ms,max_ms\n");
        }

        auto report = [&](const char* algorithm, unsigned threads, const Timing& timing,
                          double baseline) {
            if (options.csv)
                std::printf("%s,%u,%zu,%zu,%s,%u,%.3f,%.3f,%.3f\n", options.graph.family.c_str(),
                            graph.vertexCount(), graph.edgeCount(), reference.size(), algorithm,
                            threads, timing.median, timing.minimum, timing.maximum);
            else
                std::printf("%-10s %8u %10.1f %10.1f %10.1f %7.2fx\n", algorithm, threads,
                            timing.median, timing.minimum, timing.maximum,
                            baseline / timing.median);
            std::fflush(stdout);
        };

        const Timing sequential = measure(options, [&] { return sequentialScc(graph); }, nullptr);
        report("sequential", 1, sequential, sequential.median);

        for (const std::string& algorithm : options.algorithms) {
            for (const unsigned threads : options.threads) {
                const Timing timing = measure(
                    options,
                    [&] {
                        return algorithm == "hybrid" ? hybridScc(graph, threads)
                                                     : parallelScc(graph, threads);
                    },
                    options.verify ? &reference : nullptr);
                report(algorithm.c_str(), threads, timing, sequential.median);
            }
        }

        if (!options.csv)
            std::printf("peak resident memory %.0f MB%s\n", peakResidentMegabytes(),
                        options.verify ? "; every parallel run matched the sequential partition"
                                       : "");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
