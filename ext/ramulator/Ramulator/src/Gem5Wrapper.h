/***********************************************************
 * 
 *   Abdelrhman Mohamed Abotaleb
 *   email:  abotalea@mcmaster.ca
 *           aabotaleb@eng.cu.edu.eg
 *   Add overloaded "send" function that passes pointer 
 *        to the intelligent prefetcher  to the wrapper.
 * ********************************************************
*/

#ifndef __GEM5_WRAPPER_H
#define __GEM5_WRAPPER_H

#include <string>

#include "Config.h"
#include "iPrefetcherCircBuffer.h"

using namespace std;

namespace ramulator
{

class Request;
class MemoryBase;

class Gem5Wrapper 
{
private:
    MemoryBase *mem;
public:
    double tCK;
    Gem5Wrapper(const Config& configs, std::string cmdTracePath, int cacheline);
    ~Gem5Wrapper();
    void tick();
    bool send(Request &req);
    //Abotaleb: Send Pointer to The Intelligent Batcher Inside MC
    bool updatePrefetcher(int ctrlNum ,circBuffer <ramulator::iBufferKeyClass, ramulator::IpreftecherQueueEntry>* iBatchBuffer[]);
    int get_num_cores(int ctrlNum);
    int get_ctrls_num();
    void finish(void);
};

} /*namespace ramulator*/

#endif /*__GEM5_WRAPPER_H*/
