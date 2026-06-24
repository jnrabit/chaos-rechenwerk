#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <fstream>
#include <x86intrin.h>   // __rdtsc
static uint64_t raw_jitter(){
    uint64_t a=__rdtsc();
    volatile float x=0.1f;
    for(int i=0;i<1000;i++) x += std::sin((float)i);   // feste Last (v11-Muster)
    uint64_t b=__rdtsc();
    return b-a;
}
int main(int argc,char**argv){
    int N = (argc>1)? atoi(argv[1]) : 100000;
    for(int i=0;i<1000;i++) (void)raw_jitter();         // Warmup verwerfen
    std::vector<uint64_t> d(N);
    for(int i=0;i<N;i++) d[i]=raw_jitter();
    std::ofstream of(argv[2]?argv[2]:"/tmp/jit.bin", std::ios::binary);
    of.write((char*)d.data(), N*sizeof(uint64_t));
    return 0;
}
