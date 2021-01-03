//
//  algorithmManager.cpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/3/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#include "suspensionManager.hpp"


/*
 Inputs:  @worker:   Object encapsulating thread specific memory
          @Sn:       The search that is now blocked on the cell conflictCell
          @conflictCell cell that Sn is blocked on
          @conflictCellAge the age of the cell object when it held the vertex that
          is now potentially blocking Sn **
 
 Pre:     The search Sn has reached a cell that is not complete and hence wants to suspend
          Sn.cellBlockedOn == nullptr is a precondition
 
 Purpose: This function is called in the event that a search encounters a cell on another search's stack
          so that the search can suspend until that cell is assigned to a strongly connected component (SCC).
          When two or more searches acquire cells in the same SCC, they will eventually run into each other and
          block on one another's searches forming a cycle. The cycle cannot be resolved on its own
          since no one search owns all the cells in the SCC. Before exiting, suspend()
          checks to see if the suspension of Sn formed a cycle that must be resolved by our
          cellTransfer() protocol. Cycle detection is done as follows:
 
          Cycle Detection:

          First, we check if the search owning conflictCell, S0, is itself suspended. If no, clearly
          there is no cycle. If S0 is suspended on a cell owned by a search S1, we repeat the process for S1.
          That is, if S1 is not suspended, we conclude there is no cycle. Otherwise we apply the process
          to S2, the search owning the cell S1 is suspended on. This process is repeated until either a) We reach
          an Si that isn't suspended or b) we reach an Si such that Si = Sn.
 
          In the first case, we can be sure there is no cycle. In the case of b) the path we took
          from S0 to Sn is a candidate for a cycle. The path is stored in the vector S. While constructing the
          path, we also keep a vector of cells, C, such that C[i] is the cell owned by S[i] that S[i-1] is  blocked on.
          Lastly, vectors A and L record the ages of each Ci and Si respectively
 
          The contents of the vector S and C can be visually represented as
 
          S0 ----> S1  ---->S2 --->...--->  Sn-1 -----> Sn ----> (to S0 to complete loop)
           ^        ^        ^                ^          ^
           |        |        |                |          |
           |        |        |                |          |
           |        |        |                |          |
           C0       C1       C2               Cn-1       Cn
 
           We travel the path a second time to verify that a cycle indeed exists. If anything
           about the path is different (i.e. different search or conflict cells, different age of the searches,
           a cell became complete, etc) we can confirm that suspending Sn did not immediately cause
           a cycle and we can safely suspend. If the path is unchanged, there is indeed a cycle that must
           be resolved. If two or more searches suspend at approximately the same time to complete a cycle,
           the cycle could be detected by more than one thread. To prevent multiple threads from trying to
           resolve the same  cycle, upon detecting a cycle a thread will make the age variable of the 
           search with the lowest pointer odd to indicate that this cycle is already being resolved.
           Race conditions are avoided by using a Compare_and_Exchange operation to increment age
 
           It is impossible for a search to suspend on the same cell twice. Therefore, if the second pass
           finds that each search is still suspended on the same cell it were suspended on
           during the first pass, it must have been that they were suspended the entire time. Thus,  in between
           the first and second passes, all the searches Si must have been suspended on Ci+1 (mod n), confirming the cycle
 
 
 
 
 
           a-->b-->c-->a
 
 
 Post:    (No Cycle) Sn is suspended on conflictCell
          
          (With Cycle) The cycle was resolved by transferring all the cells known to be in
          the same SCC as the conflict cells to Sn's  stacks. Sn is free to resume execution as it now owns the
          cell it was blocked on. Any Si in the resolved cycle whose tarjanStacks are empty
          following the transfers have their memory recycled by the thread for reuse in representing
          future searches


 Returns:  true(SUSPEND) if Sn was suspended, false(RESUME) can continue immediately after the function returns



Note (*) On the second pass, we must check if Ci is still on Si's stack before verifying that the cell's age
 is unchanged. To see why, consider a situation where A[i] = Ci->age is read for some i before the current 
 thread is descheduled and another thread completes Ci and recycles it. When the thread resumes, it will read 
 that Ci is not complete since Ci's memory is being used for new cell. Thus, we read the age variable second.

*/


bool SuspensionManager::suspend(Worker& worker, Search* const Sn, Cell<Vid>* const conflictCell){

    const Vid Sn_Age(Sn->age);
    
    conflictCell->addToBlockedList(Sn);
    
    //Conflict cell seems to be in progress, so mark this search as suspended
    Sn->suspendOn(conflictCell);
    
    /*We must check that the cell did not become complete while we suspended the 
     search on it as its unsuspend() function may have been called before 
     the search was "officially" suspended, hence leaving it forever suspended.
     If the cell did become complete, we try to manually unsuspended it.
     If the unsuspend succeeds, we can resume. Otherwise, we must suspend as
     the search may have already been put on pending and we do not want do threads to run it 
     at the same time
     */
    
    //In case resumeAll() already happened, we do its work for it and remove cell blocked on

    
    if(conflictCell->isComplete()){
        if(Sn->suspendedOnCompareAndExchange(conflictCell, nullptr))
            return RESUME;
        else
            return SUSPEND;

    }

    //We are reusing old empty vectors to represent the path of searches and cells
    //We reuse the memory to avoid allocating memory too much as memory is often
    //allocated sequentially on standard systems, hence presenting a potential bottleneck
    
    worker.cleanPaths(); //Refresh memory from previous usage
    std::vector<Search*>&    S = worker.S; //vector to record the path from S0 to Sn
    std::vector<Cell<Vid>*>& C = worker.C; //Ci is the cell of Si that the previous search blocks on
    std::vector<Vid>&        L = worker.L; //Li = age of the search object Si
    std::vector<Vid>&        A = worker.A; //Ai = age of the cell object Ci

    
    /******* Check for cycle ********/
    
    //Path: first pass
    
    Search* Si(Sn); Cell<Vid>* Ci; Vid Li, Ai;
    
    //is there already a blocking path from S1 to Sn?
    
    
    /*S0 --> S1 -->  S2 --> S3 -->...---> Sn--> (loop) S0
     ^       ^      ^       ^            ^
     |       |      |       |            |
     C0      C1    C2       C3           Cn
     
     */
    
    do{
        Ci = Si->getBlockingCell();
        if(!Ci) return SUSPEND;
        
        //We want to record the age of the cell Si was suspended on, hence we check
        //to make sure Si is still suspended on Ci after reading the age variable
        Ai = Ci->age;
        if(Si->getBlockingCell() != Ci) return SUSPEND;
    
        Si = Ci->getOwner();
        if(!Si) return SUSPEND;
        
        Li = Si->age;
    
        //Odd numbers signal that the search is currently in a transfer, so we can safely
        //suspend knowing any cycles are being resolved 
        if(Li & 1)
            return SUSPEND;
            
        C.push_back(Ci); S.push_back(Si); L.push_back(Li); A.push_back(Ai);
        
        
    }while(Si != Sn);
    
    //Second pass
    
    Si = Sn;
    
    int minPtr(0);  //Records the index of search stored at the lowest memory address
    const int pathSize(S.size());
    
    for(int i = 0; i < pathSize; ++i){
        
        Ci = Si->getBlockingCell();
        
        //Make sure the search's age has not changed in between the read of cellBlockedOn in the first pass
        //and the one above
        if(Si->age != L[(i-1+pathSize) % pathSize])
            return SUSPEND;
        
        if(Ci != C[i])
            return SUSPEND;
        
        Si = Ci->getOwner(); //Checks completeness as well, returning null if Ci is complete
        
        if(Si != S[i])
            return SUSPEND;
        
        //We checked completeness first! See note (*) above
        if(Ci->age != A[i])
            return SUSPEND;
        
        if(Si < S[minPtr])
            minPtr = i;
        
        
    }
    //If Sn became suspended on a different cell or Sn was finished and recycled,
    // a later thread calling suspend() can handle any cycles it is involved in
    if(Sn->getBlockingCell() != conflictCell || Sn_Age != L[pathSize-1])
        return SUSPEND;
    
    /* If we get here, a loop is confirmed but we need a "consensus protocol"
     * so that more than one thread does not attempt to fix the same cycle.
     * We increment the age of the search with the smallest pointer by one
     * Making it odd. This is why we check if the ages of searches are odd when building path
     */
    
    if(!S[minPtr]->age.compare_exchange_strong(L[minPtr], L[minPtr]+1))
        return SUSPEND;
    
    /* Sn can resume after cell transfer protocal transfers the conflicting cell to Sn
     * Sn's cellBlockedOn field is made null before the transfer to prevent any false cycle detections during the
     * intermediate steps
     */
    
    Sn->removeCellBlockedOn();

    
    runCellTransfer(S, C); //Resolve the cycle
    

    ++(S[minPtr]->age); //Make age of search even again to signal that the search is no longer in transfer
    
    for(Search* search: S)
        if(search->done()) //Reclaim memory from the finished search
            worker.reclaim(search);
        
    
    return RESUME;
    
}

/**
 
 
 Inputs:
 
 @param S: The blocking path from the search directly blocking S, Sn, all the way to Sn
 
           The path vector S is represented as this:
 
           S0 ----> S1  ---->S2 --->...--->  Sn-1 -----> Sn ----> (to S0 to complete loop)
 
 @param C:
           The vector C is the list of Cells Ci, 0 <= i <= n where Ci belongs to Si and the search Si-1
           is blocked on Ci. i.e. Sn blocks on C0 which is owned by S0. S0 is suspended on C1 which is
           owned by S1 and so on. 
 
 Pre:      The searches S0 is blocked on a cell C1 of search S1, the search S1 is blocked on a cell C2 of S2,...,
           the search Sn is blocked on a cell Sn
 
 Purpose:  Blocking Cycles are formed when two or more searches explore the same SCC. 
           We refer to this SCC as the conflict SCC.   The problem is that
           cells only become "complete" when a search has found every cell in that SCC. So when multiple searches
           explore the same SCC, each one collects some cells of the SCC until they are forced to suspend on one
           another as they try to collect the entire SCC. The searches cannot resume since they are blocked on one another
           (more specifically searches block on cells and do not resume until the cells are complete but the cells 
            can't become complete since no search owns the whole SCC.
 
           Since the problem is that the searches S0 to Sn all own cells from the same SCC, the conflict SCC,
           the solution is to transfer all those cells to one of the searches. The role of this function is to do
           just that. The function transfers the cells known to be in the conflict set from searches S0...Sn-1  to
           the search Sn.
 
 Post:     The cells of S0 to Sn-1 known to be in the conflict set are transferred to Sn
           The exact reasoning is provided in the paper associated with this program
 
           S0 to Sn-1 are now all blocked on the last cell that was transferred from their tarjanStacks,
           which are now owned by Sn
 
           The controlStack top of Sn has its rank lowered to account for all the new cells transferred
 

 
 **/

void SuspensionManager::runCellTransfer(const std::vector<Search*>& S, const std::vector<Cell<Vid>*>& C)
{
    Search* const Sn(S.back());
    
    
    //Traverse the path starting with the search that S is directly blocked on
    for(int i = 0; i < S.size() - 1; ++i)
        Search::transferCells(S[i], Sn, C[i]);
    
    //Makes sure S is "aware" that all transferred cells are in the same SCC,
    //make the new top of S's tarjan stack (last of the transferred cells) have rank
    //of the cell of S that S(n-1) was blocked on
    
    Sn->controlStackTop()->promote(C.back()->index);
    
}




/**

 Summary: This function puts all the searches blocked on a cell that has become complete
          on the Pending object for resumption

 @param completeCell  The cell that has recently become complete
 @param blockedList The list of searches that may be blocked on completeCell
 @param toResume      A vector that will hold the searches that must be put on the Pending object for resumption
                 The Pending Queue is accessed by worker threads that
                 have finished their previous jobs and are looking for new searches to work on

 Purpose: The code looks a bit complicated because it is optimized for common conditions the algorithm faces 
          Basically, after a cell is completed all the searches blocked on that cell must be resumed. Some of
          the searches in the BlockedList; however, will no longer actually be suspended on the cell by the time this
          function is called. The reason for this is that searches will sometimes unblock on a cell for
          reasons other than it being complete (i.e. during a transfer) but it is more efficent to leave them in the
          blockedList anyway as it allows a simpler of blockedList. To insure no searches that are not actually suspended
          on blockedList are accidently resumed, we use a compare_and_exchange to verify that the search was actually
          suspended on the cell
 
 */
void SuspensionManager::bulkUnsuspend(BlockedList<Search*>* const blockedList, Cell<Vid>* const completeCell, std::vector<Search*>& toResume){
    
    Search* search; const int size(blockedList->size());
    
    //Cells will very rarely have more than a few searches suspended on them, so we optimize
    //our algorithm to quickly deal with suspended. This allows us to save memory and time at the cost of
    //slightly longer stores and retrievals when the suspended list is longer than 8.
    //Because most graphs are large, it will be very rare when more than a few searches suspend on the same cell
    //so the trade off is well worth it
    
    if(blockedList->isSmallList(size)){
        std::atomic<Search*>* const suspendedSearches{blockedList->getSmallBuffer()};
        for(int e = 0; e < size; ++e){
            search = suspendedSearches[e].load(std::memory_order_relaxed);
            if(search && search->removeCellBlockedOnWithCompareExchange(completeCell))
                toResume.push_back(search);
        }
    }
    
    else{
        
        auto suspendedListIt(blockedList->getIt(size));
        
        while(suspendedListIt.increment()){
            search = suspendedListIt.getNext();
            
            if(search && search->removeCellBlockedOnWithCompareExchange(completeCell))
                toResume.push_back(search);
            
        }
        
    }
    
    
}





