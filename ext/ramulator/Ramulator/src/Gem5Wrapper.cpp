#include <map>

#include "Gem5Wrapper.h"
#include "Config.h"
#include "Request.h"
#include "MemoryFactory.h"
#include "Memory.h"
#include "DDR3.h"
#include "DDR4.h"
#include "LPDDR3.h"
#include "LPDDR4.h"
#include "GDDR5.h"
#include "WideIO.h"
#include "WideIO2.h"
#include "HBM.h"
#include "SALP.h"

using namespace ramulator;

static map<string, function<MemoryBase *(const Config&,std::string, int)> > name_to_func = {
    {"DDR3", &MemoryFactory<DDR3>::create}, {"DDR4", &MemoryFactory<DDR4>::create},
    {"LPDDR3", &MemoryFactory<LPDDR3>::create}, {"LPDDR4", &MemoryFactory<LPDDR4>::create},
    {"GDDR5", &MemoryFactory<GDDR5>::create}, 
    {"WideIO", &MemoryFactory<WideIO>::create}, {"WideIO2", &MemoryFactory<WideIO2>::create},
    {"HBM", &MemoryFactory<HBM>::create},
    {"SALP-1", &MemoryFactory<SALP>::create}, {"SALP-2", &MemoryFactory<SALP>::create}, {"SALP-MASA", &MemoryFactory<SALP>::create},
};


Gem5Wrapper::Gem5Wrapper(const Config& configs, std::string cmdTracePath, int cacheline)
{
    const string& std_name = configs["standard"];
    /*map<string, function<MemoryBase *(const Config&, int)> >::iterator it,found;

    for (it = name_to_func.begin(); it != name_to_func.end(); it++)
    {
        printf("Current key : (%s) \n", it->first.c_str() );
 
    }

    printf("configs[\"standard\"] = (%s)\n",std_name.c_str());
    printf("find std name res = %s\n",name_to_func.find(std_name)->first.c_str());
    printf("fname_to_func.end() = %d\n",name_to_func.end());*/
    
    assert(name_to_func.find(std_name) != name_to_func.end() && "unrecognized standard name");
    mem = name_to_func[std_name](configs,cmdTracePath, cacheline);
    //MemoryFactory<DDR4>::create is called here 
    // ctrls with channels = channel number from spec  is created here 
    tCK = mem->clk_ns();
}


Gem5Wrapper::~Gem5Wrapper() {
    delete mem;
}

void Gem5Wrapper::tick()
{
    mem->tick();
}

bool Gem5Wrapper::send(Request& req)
{
    return mem->send(req);
}

//Abotaleb
bool Gem5Wrapper::updatePrefetcher(int ctrlNum ,circBuffer<ramulator::iBufferKeyClass, ramulator::IpreftecherQueueEntry> * iBatchBuffer[])
{
    return mem->updatePrefetcher(ctrlNum ,iBatchBuffer);
}
int Gem5Wrapper::get_num_cores(int ctrlNum)
{
    return mem->get_num_cores(ctrlNum);
}
int Gem5Wrapper::get_ctrls_num()
{
    return mem->get_ctrls_num();
}
void Gem5Wrapper::finish(void) {
    mem->finish();
}
