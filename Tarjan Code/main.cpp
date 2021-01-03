//
//  main.cpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/24/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#include <iostream>
#include "typedefs.h"
#include "unordered_map"
#include "search.hpp"
#include "tarjan.hpp"
#include <thread>
#include "simpleClock.h"
#include "utilities.hpp"
#include "SimpleSharded.h"
#include "suspensionManager.hpp"
#include <dirent.h>
#include <algorithm>

using namespace std;
void benchmarkHashTables();
void testWithRandomGraphs();
void testMemoryUse(); void testMemoryUse(string);

void testWithCSPGraphs(string filename);
void testSingles();
void deleteSCCs(SCC_Set*);
void benchmark_CSP(string dir, string filename, bool benchSingle = true, const int RUNS = 100, const std::initializer_list<int> = {8});
void benchmark_CSP_Dir(string dir,  bool benchSingle = true, const int RUNS = 100, const std::initializer_list<int> = {8});
void benchmarkGraph(Graph<Vid>* graph, string name, bool benchSingle = true, const int RUNS = 100, const std::initializer_list<int> = {8});
void benchmark_GeoGen(double , Vid , bool = true, const int  = 100, const std::initializer_list<int> = {8});
void benchmark_Clusters(Vid , Vid , bool = true, const int  = 100, const std::initializer_list<int> = {8});

#define DISPLAY_SCC_COUNT false
#define INIT_RUNS 0


void sleeps(int secs){
    std::this_thread::sleep_for(std::chrono::seconds(secs));
}
std::vector<std::string> GetDirectoryFiles(const std::string& dir) {
    std::vector<std::string> files;
    std::shared_ptr<DIR> directory_ptr(opendir(dir.c_str()), [](DIR* dir){ dir && closedir(dir); });
    struct dirent *dirent_ptr;
    if (!directory_ptr) {
        std::cout << "Error opening : " << dir << std::endl;
        return files;
    }
    
    while ((dirent_ptr = readdir(directory_ptr.get())) != nullptr) {
        files.push_back(std::string(dirent_ptr->d_name));
    }
    return files;
}

#define DIRECTORY "/Users/alex/Downloads/CSP/Alt/"

int main(int argc, const char * argv[]) {
/*
    OpenAddressedMap map;
    Cell<Vid> cell;
    for(int i = 0; i < 10000; ++i)
        cout << map.put(i % 1000, WeakReference<Cell<Vid>>(&cell,4)).second << endl;;
    cout << map.put(5, WeakReference<Cell<Vid>>(&cell,4)).second << endl;
*/
    
 //  testWithRandomGraphs();
    
    // testWithCSPGraphs("/Users/alex/Downloads/CSP/Alt/alt10.2.4.graph");
    
    //testWithCSPGraphs("/Users/alex/Downloads/CSP/alt/alt10.3.0.graph");
    //2.02
    //testWithCSPGraphs("/Users/alex/Downloads/CSP/bill_timed/should_pass/tabp.7.graph");
    
    /*
    const auto& directory_path = std::string("/Users/alex/Downloads/CSP/alt/.");
    const auto& files = GetDirectoryFiles(directory_path);
    for (const auto& file : files) {
        std::cout << file << std::endl;
    }*/
    //2.4 3.13
   benchmark_CSP("/Users/alex/Downloads/CSP/Alt/", "alt10.2.4.graph", false, 3, {8});
  //  benchmark_CSP("/Users/alex/Downloads/CSP/Alt/", "alt10.3.4.graph", 1, 4, {2,4,8});

//    benchmark_CSP("/Users/alex/Downloads/CSP/Alt/", "alt10.3.3.graph", 0, 10, {8});
   // benchmark_CSP("/Users/alex/Downloads/CSP/bill_timed/should_pass/", "cloudp.0.graph", 1, 5, {2,4,8});
     // benchmark_CSP("/Users/alex/Downloads/CSP/Alt/", "alt11.2.0.graph", 1, 20, {2,4,8});
   //   benchmark_CSP("/Users/alex/Downloads/CSP/Alt/", "alt11.2.0.graph", 1, 15, {2,4,8,10});
      //  benchmark_CSP("/Users/alex/Downloads/CSP/Alt/", "alt10.2.7.graph", 1, 15, {2,4,8});
//      benchmark_CSP("/Users/alex/Downloads/CSP/Alt/", "alt10.4.3.graph", 1, 11, {2,4,8});
    //benchmark_Clusters(30000, 1000,0,40, {8});
   // benchmark_Clusters(50000, 1000,0,10, {8});

  // benchmark_GeoGen(0.00057, 30000, true, 50, 8);
    
  //  benchmark_CSP("/Users/alex/Downloads/CSP/bill_timed/should_pass/", "cloudp.0.graph", 1, 10, {2,4,8});
    //testMemoryUse("/Users/alex/Downloads/CSP/Alt/alt10.3.4.graph");
    //testMemoryUse();
    
     //benchmark_CSP_Dir("/Users/alex/Documents/workspace/CStuff 3/Tarjan/graphs/", true, 30, {2,4,8});

        //after 2.3
  // benchmark_CSP_Dir("/Users/alex/Downloads/CSP/Alt/", 0, 20, {8});
    //benchmark_CSP_Dir("/Users/alex/Downloads/CSP/bill_timed/should_pass/", true, 30, {2,4,8});
//    benchmark_CSP_Dir("/Users/alex/Downloads/CSP/tpc_examples/should_pass/", true, 30, {2,4,8});

  // benchmark_CSP("/Users/alex/Downloads/CSP/tpc_examples//should_pass/", "matmul.6.graph", 1, 10, {2,4,8});

   // benchmark_CSP(DIRECTORY, "alt10.3.0.graph", 0, 100, {8});

 //   benchmark_CSP(DIRECTORY, "alt10.3.4.graph", 0, 3, 4);

 // benchmark_CSP(DIRECTORY, "alt10.3.4.graph", 0, 3, 8);
  //  benchmark_CSP(DIRECTORY, "alt10.2.2.graph", 1, 5, {2,4,8});
 //   benchmark_GeoGen(0.00057, 100000, true, 50, 8);
  //  benchmark_CSP("/Users/alex/Downloads/CSP/bill_timed/should_pass/", "tring2.1.graph", 1, 20, {8});
    
//    benchmark_Clusters(1000, 1000,false, 1, {8});
//    benchmark_Clusters(10000, 1000,false,  1, {8});
//    benchmark_Clusters(25000, 1000,false,  1, {8});;
//    benchmark_Clusters(50000, 1000,false,  1, {8});
//


    //9.22837 seconds.
    
   // benchmark_CSP("/Users/alex/Downloads/CSP/Alt/", "alt10.3.2.graph", 1, 5, 8);
    
  //  benchmarkHashTables();

    
    return 0;
}

void testSingles(){
    
    DirectedHashGraph<Vid> graph;
    
    graph.insertEdge(0,4);  graph.insertEdge(4,5); graph.insertEdge(5,6); graph.insertEdge(5, 5);
    graph.insertEdge(6, 4); graph.insertEdge(0, 5);
    graph.insertEdge(10, 15);     graph.insertEdge(15, 10);


    graph.insertEdge(10, 106);
    graph.insertEdge(101, 102);
    graph.insertEdge(102, 103);
    graph.insertEdge(103, 101);
    graph.insertEdge(104, 103);
    graph.insertEdge(104, 102);
    graph.insertEdge(104, 105);
    graph.insertEdge(105, 105);
    graph.insertEdge(105, 104);
    graph.insertEdge(105, 106);
    graph.insertEdge(106, 103);
    graph.insertEdge(106, 107);
    graph.insertEdge(107, 106);
    graph.insertEdge(108, 108);
    graph.insertEdge(108, 107);
    graph.insertEdge(108, 109);
    graph.insertEdge(109, 110);
    graph.insertEdge(110, 111);
    graph.insertEdge(109, 112);
    graph.insertEdge(112, 112);
    graph.insertEdge(113, 112);
    graph.insertEdge(113, 114);
    graph.insertEdge(114, 113);
    graph.insertEdge(114, 109);
    graph.insertEdge(200, 201);
    graph.insertEdge(200, 202);
    graph.insertEdge(201, 203);
    graph.insertEdge(201, 205);
    graph.insertEdge(202, 206);
    graph.insertEdge(203, 205);
    graph.insertEdge(204, 202);
    graph.insertEdge(205, 201);
    graph.insertEdge(205, 207);
    graph.insertEdge(206, 204);
    graph.insertEdge(207, 205);
    graph.insertEdge(209, 203);
    


    SCC_Set* SCCs  = Tarjan::singleThreadedTarjan(graph);
    SCC_Set* SCCs2 = Tarjan::multiThreadedTarjan(graph);

    
    for(auto& set: *SCCs){
        
        cout<< "(";
        
        for(auto& vert: *set)
            cout << vert << " ";
        
        
        
        cout <<")" << endl;
        
    }
    
    cout << SCCs->size() << " " << SCCs2->size() <<endl;
    
    delete SCCs; delete SCCs2;
    
    //7.91
    
}
//.915

void testMemoryUse(){
     Graph<Vid>* graph   = Utility::clusters(10000, 1000, 6, 50);
    
   // Graph<Vid>* graph = Utility::GeoGenerateRandomGraph(0.00057, 1, 50000);

    
    std::cout<< "graph size: " << graph->size() <<endl;
    std::this_thread::sleep_for(2s);
    cout << "all recycle enabled" << endl;
    auto sccs = Tarjan::multiThreadedTarjan(*graph,8);
    Utility::deleteSCCs(sccs);
    /*
    cellDisable = 0; searchDisable = 1;
    cout << "cell recycle enabled" << endl;
    sccs = Tarjan::multiThreadedTarjan(*graph,8);
    Utility::deleteSCCs(sccs);
    
    cellDisable = 1; searchDisable = 0;
    cout << "search recycle enabled" << endl;
    sccs = Tarjan::multiThreadedTarjan(*graph,8);
    Utility::deleteSCCs(sccs);
    
    cellDisable = 1; searchDisable = 1;
    cout << "all recycle disable" << endl; 
    sccs = Tarjan::multiThreadedTarjan(*graph,8);
    Utility::deleteSCCs(sccs);
    */
    delete graph;
    
}

void testMemoryUse(string path){
    Graph<Vid>* const graph = Utility::importGraphFromCSP(path);
    
    // Graph<Vid>* graph = Utility::GeoGenerateRandomGraph(0.00057, 1, 50000);
    
    std::cout<< "graph size: " << graph->size() <<endl;
    std::this_thread::sleep_for(2s);
    cout << "all recycle enabled" << endl;
    auto sccs = Tarjan::multiThreadedTarjan(*graph,8);
    Utility::deleteSCCs(sccs);
    /*
     cellDisable = 0; searchDisable = 1;
     cout << "cell recycle enabled" << endl;
     sccs = Tarjan::multiThreadedTarjan(*graph,8);
     Utility::deleteSCCs(sccs);
     
     cellDisable = 1; searchDisable = 0;
     cout << "search recycle enabled" << endl;
     sccs = Tarjan::multiThreadedTarjan(*graph,8);
     Utility::deleteSCCs(sccs);
     
     cellDisable = 1; searchDisable = 1;
     cout << "all recycle disable" << endl;
     sccs = Tarjan::multiThreadedTarjan(*graph,8);
     Utility::deleteSCCs(sccs);
     */
    delete graph;
    
}


void testWithRandomGraphs(){
    // insert code here...
    
    SimpleClock c;

    Graph<Vid>* graph  = Utility::generateRandomGraph(0.0001,40000);

   // Graph<Vid>* graph  = Utility::GeoGenerateRandomGraph(0.00057, 1, 30000);
   
  //  Graph<Vid>* graph   = Utility::clusters(10000, 1000, 6, 1000);
    
    
    const int N(500);
    SCC_Set* sets[N];
    
    for(int i = 0; i < N; ++i){
        
        c.begin();
        sets[i] = Tarjan::multiThreadedTarjan(*graph,8);
        c.end();
        //if(!i)
        //    for(auto set: *sets[i]){
          //      cout << set->size() <<endl;
          //  }
        cout << "searches leaked " << s << endl;
        cout << sets[i]->size() <<endl;
        Utility::deleteSCCs(sets[i]);
    }
    //c.printAccum("accumulated: ");  // 3.27669
    
    c.begin();
    auto sccs = Tarjan::singleThreadedTarjan(*graph);
    c.end("Single Threaded Tarjan");
    
    c.begin();
    auto sccs2 = Tarjan::singleThreadedTarjan(*graph);
    c.end("Single Threaded Tarjan");
    
 
    cout << sccs->size() << endl;

    cout << sccs2->size() << endl;
    
    //for(int i =0; i < N; ++i)
    //    cout << sets[i]->size() <<endl;

    
//    int set1Card(0); int set2Card(0);
//    for(auto set: *sccs){
//        set1Card+= set->size();
//        
//    }
//    
//    for(auto set: *sccs2){
//        set2Card+= set->size();
//    }
//    
    
//    cout << set1Card << "   :   " << set2Card <<endl;


    Utility::deleteSCCs(sccs); Utility::deleteSCCs(sccs2);
    /*
    c.begin();
    auto sccs3 = Tarjan::singleThreadedTarjan(*graph);
    c.end("Single Threaded Tarjan Run 3");
    Utility::deleteSCCs(sccs3);
    
    c.begin();
    auto sccs4 = Tarjan::singleThreadedTarjan(*graph);
    c.end("Single Threaded Tarjan Run 4");
    Utility::deleteSCCs(sccs4);
    
    c.begin();
    auto sccs5 = Tarjan::singleThreadedTarjan(*graph);
    c.end("Single Threaded Tarjan Run 5");
    Utility::deleteSCCs(sccs5);*/
    
    delete  graph;
    
    
}

void testWithCSPGraphs(string filename){
    
    Graph<Vid>* graph = Utility::importGraphFromCSP(filename);
    
    std::cout << "Number of Verticies: " << graph->size() << endl;
    
    SimpleClock profiler;
    
    profiler.begin();
    auto sccs = Tarjan::singleThreadedTarjan(*graph);
    profiler.end();
    
    
    cout << sccs->size() <<endl;
    
    const int N(20);
    SCC_Set* sets[N];
    
    for(int i = 0; i < N; ++i){
        
        profiler.begin();
        sets[i] = Tarjan::multiThreadedTarjan(*graph,8);
        profiler.end();
        //if(!i)
        //    for(auto set: *sets[i]){
        //      cout << set->size() <<endl;
        //  }
        cout << "searches leaked " << s << endl;
        cout << sets[i]->size() <<endl;
        Utility::deleteSCCs(sets[i]);
    }
    
    
    
    profiler.begin();
    auto sccs2 = Tarjan::multiThreadedTarjan(*graph, 8);
    profiler.end("Multi Threaded Tarjan");
    
    profiler.begin();
    auto sccs3 = Tarjan::singleThreadedTarjan(*graph);
    profiler.end("Single Threaded Tarjan");
    
    profiler.begin();
    auto sccs4 = Tarjan::singleThreadedTarjan(*graph);
    profiler.end("Single Threaded Tarjan");
    
    
    cout << sccs->size() << endl;
    
    cout << sccs2->size() << endl;
    
    //for(int i =0; i < N; ++i)
    //    cout << sets[i]->size() <<endl;
    
    
    
    int set1Card(0); int set2Card(0);
    for(auto set: *sccs){
        set1Card+= set->size();
        
    }
    
    for(auto set: *sccs2){
        set2Card+= set->size();
    }
    
    
    cout << set1Card << "   :   " << set2Card <<endl;
    
    delete graph;
    Utility::deleteSCCs(sccs); Utility::deleteSCCs(sccs2);
    Utility::deleteSCCs(sccs3); Utility::deleteSCCs(sccs4);

}

void benchmarkGraph(Graph<Vid>* graph, string name, bool benchSingle, const int RUNS, const std::initializer_list<int> THREADS){
    
    SimpleClock profiler; string msg;
    
    //BenchMark Single Threaded
    
    if(benchSingle){
        for(int run = 0; run < INIT_RUNS; ++run){
            auto sccs = Tarjan::singleThreadedTarjan(*graph);
            Utility::deleteSCCs(sccs);
        }
        
        for(int run = 0; run < RUNS; ++run){
            profiler.begin();
            auto sccs = Tarjan::singleThreadedTarjan(*graph);
            profiler.accumulate();
            Utility::deleteSCCs(sccs);
            
        }
        msg = "Single Threaded " + name;
        profiler.printAccum(std::move(msg), RUNS);
        profiler.resetAccum();
        
    }
    
    for(int THDS : THREADS){
        for(int run = 0; run < INIT_RUNS; ++run){
            auto sccs = Tarjan::multiThreadedTarjan(*graph, THDS);
            Utility::deleteSCCs(sccs);
        }
        
        for(int run = 0; run < RUNS; ++run){
            profiler.begin();
            auto sccs = Tarjan::multiThreadedTarjan(*graph, THDS);
            profiler.accumulate();
            if(DISPLAY_SCC_COUNT) std::cout<< sccs->size() << ",";
            Utility::deleteSCCs(sccs);
        }
        if(DISPLAY_SCC_COUNT) std::cout<< endl;
        
        msg = std::to_string(THDS) + " Threads " + name;
        profiler.printAccum(std::move(msg), RUNS);
        profiler.resetAccum();
        
    }
    
    delete graph;
}


void benchmark_CSP(string path, string filename, bool benchSingle, const int RUNS, const std::initializer_list<int> THREADS){

    Graph<Vid>* const graph = Utility::importGraphFromCSP(path+filename);
    
    benchmarkGraph(graph, filename, benchSingle, RUNS, THREADS);

}

void benchmark_Clusters(Vid clusters, Vid clustSize, bool benchSingle, const int RUNS, const std::initializer_list<int> THREADS){
    
    Graph<Vid>* const graph   = Utility::clusters(clusters, clustSize, 8, 50);
    
    string graphName = "Clust_" + to_string(clusters) + "X" + to_string(clustSize);
    
    std::cout << graph->numberEdges() << std::endl;
    
    benchmarkGraph(graph, graphName, benchSingle, RUNS, THREADS);
    
}

void benchmark_GeoGen(double edgeProb, Vid cardinality, bool benchSingle, const int RUNS, const std::initializer_list<int> THREADS){
    
    Graph<Vid>* graph = Utility::GeoGenerateRandomGraph(edgeProb, 1, cardinality);

    string graphName = "Geo_" + to_string(cardinality);
    
    benchmarkGraph(graph, graphName, benchSingle, RUNS, THREADS);
    
}

bool hasEnding (std::string const &fullString, std::string const &ending) {
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

static unordered_set<string> toAvoid = {"alt10.2.0.graph", "alt10.3.3.graph", "matmul.6.graph", "alt10.3.4.graph", "cloudp.0.graph", "alt10.3.1.graph"};

void benchmark_CSP_Dir(string dir,  bool benchSingle, const int RUNS, const std::initializer_list<int> THREADS){
    
    
    vector<string> graphs = GetDirectoryFiles(dir);
    auto it = graphs.begin();
    
    while(it != graphs.end()){
        if(!hasEnding(*it, ".graph") || toAvoid.count(*it))
            it = graphs.erase(it);
        else ++it;
    }
    for(string file: graphs)
        benchmark_CSP(dir, file, benchSingle, RUNS, THREADS);
    
}


void benchmarkHashTables(){
    std::vector<string> graphs{"alt10.2.0.graph", "alt10.3.3.graph", "alt10.3.4.graph", "alt10.3.1.graph"};
    
    const int RUNS{2}; SimpleClock profiler;
    
    std::vector<DictType> dicts = {Sharded_Locked, Sharded_SpinLock, OpenSharded, OpenAddressed, Cuckoo};
    
    
    double* results = new double[dicts.size()*graphs.size()];
    
    for(int g = 0; g < graphs.size(); ++g){
        
        Graph<Vid>* const graph = Utility::importGraphFromCSP(DIRECTORY+graphs[g]);
        
        
        for(int d = 0; d < dicts.size(); ++d){
            
            for(int run = 0; run < RUNS; ++run){
                profiler.begin();
                auto sccs = Tarjan::multiThreadedTarjan(*graph, 8, dicts[d]);
                profiler.accumulate();
                if(DISPLAY_SCC_COUNT) std::cout<< sccs->size() << ",";
                Utility::deleteSCCs(sccs);
            }
            results[d*graphs.size()+g] = (profiler.getAccum()/RUNS);
            profiler.resetAccum();
        }
        
        delete graph;
        
    }
    
    
    for(int d = 0; d < dicts.size(); ++d){
        std::cout << dicts[d] << std::endl;
        for(int g = 0; g < graphs.size(); ++g){
            std::cout << results[d*graphs.size()+g] << std::endl;
        }
    }
    
    std::cout << "d " << std::endl;
    
    
    
    
}





