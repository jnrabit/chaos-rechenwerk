// strom_resident.cpp — Mini-Ossifikat-Opt: persistenter GPU-Kontext, ganzer Strom in EINER Session.
// Reine Geschwindigkeits-Opt (Korrektheit Nachtrag 60 steht). Context/Queue/Kernel/Buffer EINMAL,
// dann pro Strom-Tripel nur: aktive Region frisch materialisieren (kein Leck) -> Settle -> Bulk-Readback
// -> entscheiden (aufnehmen/verwerfen). Self-contained: A/B/C-Szenarien + C++-Referenz + Timing.
// SELBER run_gate-Kernel wie engine_gpu_gate.cpp (bit-gleich). double/fp64. Produktion md5-unberuehrt.
#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
using clk=std::chrono::high_resolution_clock;
static double ms(clk::time_point a,clk::time_point b){return std::chrono::duration_cast<std::chrono::microseconds>(b-a).count()/1000.0;}
static const double DT=0.005; static const int NT=2000; static const double KW=2.0; static const int MAX_SLOTS=1100;

// ---- IDENTISCHER Kernel zu engine_gpu_gate.cpp ----
const char* kernelSource = R"CLC(
#pragma OPENCL EXTENSION cl_khr_fp64 : enable
typedef struct { double x1,y1,z1,w1,x2,y2,z2,w2; } S8;
static S8 d_add(S8 a,S8 b){S8 r;r.x1=a.x1+b.x1;r.y1=a.y1+b.y1;r.z1=a.z1+b.z1;r.w1=a.w1+b.w1;
 r.x2=a.x2+b.x2;r.y2=a.y2+b.y2;r.z2=a.z2+b.z2;r.w2=a.w2+b.w2;return r;}
static S8 d_sc(double k,S8 a){S8 r;r.x1=k*a.x1;r.y1=k*a.y1;r.z1=k*a.z1;r.w1=k*a.w1;
 r.x2=k*a.x2;r.y2=k*a.y2;r.z2=k*a.z2;r.w2=k*a.w2;return r;}
static S8 deriv_g(S8 s,double W,double sig,double rho,double bet,double gk,double gnet){
  S8 d;
  d.x1=sig*(s.y1-s.x1);            d.y1=s.x1*(rho-s.z1)-s.y1+W*s.w1;
  d.z1=s.x1*s.y1-bet*s.z1;         d.w1=-s.w1+s.x1*s.z1-W*s.y1;
  d.x2=sig*(s.y2-s.x2);            d.y2=s.x2*(rho-s.z2)-s.y2+W*s.w2;
  d.z2=s.x2*s.y2-bet*s.z2;         d.w2=-s.w2+s.x2*s.z2-W*s.y2;
  if(gk!=0.0) d.w1 += gk*(gnet - s.w1);
  return d;
}
static S8 rk4_g(S8 s,double W,double sig,double rho,double bet,double DT,double gk,double gnet){
  S8 k1=deriv_g(s,W,sig,rho,bet,gk,gnet);
  S8 k2=deriv_g(d_add(s,d_sc(DT/2.0,k1)),W,sig,rho,bet,gk,gnet);
  S8 k3=deriv_g(d_add(s,d_sc(DT/2.0,k2)),W,sig,rho,bet,gk,gnet);
  S8 k4=deriv_g(d_add(s,d_sc(DT,    k3)),W,sig,rho,bet,gk,gnet);
  return d_add(s, d_sc(DT/6.0, d_add(k1, d_add(d_sc(2.0,k2), d_add(d_sc(2.0,k3), k4)))));
}
__kernel void run_gate(__global const double* in,__global double* out,
                       __global const int* gin,__global const double* gsg,
                       __global const double* gKw,__global const double* gbias,
                       int N,double W){
  int g=get_global_id(0); if(g>=N) return;
  const double SIG=10.0,RHO=28.0,BET=8.0/3.0,DTk=0.005;
  double gk=gKw[g], gnet=0.0;
  if(gk!=0.0){
    double net=gbias[g];
    for(int j=0;j<4;j++){ int idx=gin[g*4+j]; if(idx>=0) net += gsg[g*4+j]*in[idx*8+3]; }
    gnet=(net>=0.0?363.0:-363.0);
  }
  S8 s; s.x1=in[g*8+0];s.y1=in[g*8+1];s.z1=in[g*8+2];s.w1=in[g*8+3];
        s.x2=in[g*8+4];s.y2=in[g*8+5];s.z2=in[g*8+6];s.w2=in[g*8+7];
  s=rk4_g(s,W,SIG,RHO,BET,DTk,gk,gnet);
  out[g*8+0]=s.x1;out[g*8+1]=s.y1;out[g*8+2]=s.z1;out[g*8+3]=s.w1;
  out[g*8+4]=s.x2;out[g*8+5]=s.y2;out[g*8+6]=s.z2;out[g*8+7]=s.w2;
}
)CLC";

struct Triple{int s,p,o; bool operator==(const Triple&o2)const{return s==o2.s&&p==o2.p&&o==o2.o;}};
static void wing(double*c,int sgn){double a=sgn*9.6,w=sgn*363.0; c[0]=a;c[1]=a;c[2]=25;c[3]=w;c[4]=a;c[5]=a;c[6]=25;c[7]=w;}

// ---- C++-Referenz: derselbe Strom, Vorzeichen-Gleichheit, sequenziell ----
static std::vector<Triple> ref_strom(const std::vector<Triple>&strom,const std::vector<Triple>&init){
  std::vector<Triple> b=init;
  for(auto&t:strom){ bool konf=false; for(auto&e:b) if(t.s==e.s&&t.p==e.p&&t.o!=e.o){konf=true;break;}
    if(!konf) b.push_back(t); }
  return b;
}

// ---- Resident GPU-Zustand ----
struct GPU {
  cl_context ctx; cl_command_queue q; cl_kernel k;
  cl_mem bA,bB,bGin,bGsg,bGKw,bGb;
  std::vector<double> hbuf;   // Host-Materialisierung der aktiven Region (max NC_max*8)
  int NC_max;
};
static void G(std::vector<int>&gin,std::vector<double>&gsg,std::vector<double>&gKw,std::vector<double>&gb,
              int c,std::vector<int>ins,std::vector<double>sgs,double bias){
  for(int j=0;j<4;j++){gin[c*4+j]=-1;gsg[c*4+j]=0;}
  for(size_t j=0;j<ins.size();j++){gin[c*4+j]=ins[j];gsg[c*4+j]=sgs[j];}
  gKw[c]=KW; gb[c]=bias;
}
static GPU gpu_init(){
  GPU G_; cl_int err; cl_uint np=0; clGetPlatformIDs(0,nullptr,&np);
  std::vector<cl_platform_id> ps(np); clGetPlatformIDs(np,ps.data(),nullptr);
  cl_device_id dev=nullptr;
  for(auto p:ps){char pn[128]={0};clGetPlatformInfo(p,CL_PLATFORM_NAME,sizeof(pn),pn,nullptr);
    cl_device_id d=nullptr; if(clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,1,&d,nullptr)==CL_SUCCESS&&d){
      if(strstr(pn,"AMD Accelerated")){dev=d;break;} if(!dev)dev=d;}}
  char dn[256]={0}; clGetDeviceInfo(dev,CL_DEVICE_NAME,sizeof(dn),dn,nullptr); fprintf(stderr,"Device: %s (resident)\n",dn);
  G_.ctx=clCreateContext(nullptr,1,&dev,nullptr,nullptr,&err);
  G_.q=clCreateCommandQueue(G_.ctx,dev,0,&err);
  cl_program prog=clCreateProgramWithSource(G_.ctx,1,&kernelSource,nullptr,&err);
  if(clBuildProgram(prog,1,&dev,nullptr,nullptr,nullptr)!=CL_SUCCESS){char l[8192]={0};
    clGetProgramBuildInfo(prog,dev,CL_PROGRAM_BUILD_LOG,sizeof(l),l,nullptr);fprintf(stderr,"BUILD:\n%s\n",l);exit(1);}
  G_.k=clCreateKernel(prog,"run_gate",&err);
  G_.NC_max=3+13*MAX_SLOTS;
  // Config fuer ALLE MAX_SLOTS Komparatoren einmal vorbauen (unabhaengig von n_aktiv)
  std::vector<int> gin(G_.NC_max*4,-1); std::vector<double> gsg(G_.NC_max*4,0),gKw(G_.NC_max,0),gb(G_.NC_max,0);
  const int SN=0,PN=1,ON=2;
  for(int m=0;m<MAX_SLOTS;m++){ int b=3+13*m;
    G(gin,gsg,gKw,gb,b+3,{SN,b+0},{1,1},+400);  G(gin,gsg,gKw,gb,b+4,{SN,b+0},{-1,-1},+400); G(gin,gsg,gKw,gb,b+5,{b+3,b+4},{-1,-1},+400);  // XNOR_S
    G(gin,gsg,gKw,gb,b+6,{PN,b+1},{1,1},+400);  G(gin,gsg,gKw,gb,b+7,{PN,b+1},{-1,-1},+400); G(gin,gsg,gKw,gb,b+8,{b+6,b+7},{-1,-1},+400);  // XNOR_P
    G(gin,gsg,gKw,gb,b+9,{ON,b+2},{1,1},+400);  G(gin,gsg,gKw,gb,b+10,{ON,b+2},{-1,-1},+400); G(gin,gsg,gKw,gb,b+11,{b+9,b+10},{1,1},-400);  // XOR_O
    G(gin,gsg,gKw,gb,b+12,{b+5,b+8,b+11},{1,1,1},-800);                                                                                       // AND3 -> Flag
  }
  G_.bA=clCreateBuffer(G_.ctx,CL_MEM_READ_WRITE,G_.NC_max*8*sizeof(double),nullptr,&err);
  G_.bB=clCreateBuffer(G_.ctx,CL_MEM_READ_WRITE,G_.NC_max*8*sizeof(double),nullptr,&err);
  G_.bGin=clCreateBuffer(G_.ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gin.size()*sizeof(int),gin.data(),&err);
  G_.bGsg=clCreateBuffer(G_.ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gsg.size()*sizeof(double),gsg.data(),&err);
  G_.bGKw=clCreateBuffer(G_.ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gKw.size()*sizeof(double),gKw.data(),&err);
  G_.bGb=clCreateBuffer(G_.ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gb.size()*sizeof(double),gb.data(),&err);
  G_.hbuf.resize(G_.NC_max*8);
  return G_;
}
static void gpu_free(GPU&g){clReleaseMemObject(g.bA);clReleaseMemObject(g.bB);clReleaseMemObject(g.bGin);
  clReleaseMemObject(g.bGsg);clReleaseMemObject(g.bGKw);clReleaseMemObject(g.bGb);
  clReleaseKernel(g.k);clReleaseCommandQueue(g.q);clReleaseContext(g.ctx);}

// EIN Check: neues Tripel gegen die n_aktiv Slots im Store. Flags zurueck. (timing: settle/io getrennt)
static std::vector<int> gpu_check(GPU&g,const Triple&nu,const std::vector<Triple>&store,double*t_io,double*t_settle){
  int N=store.size(); if(N==0) return {};
  int NC=3+13*N; cl_int err;
  // --- aktive Region FRISCH materialisieren (kein Leck) ---
  double*h=g.hbuf.data();
  // Broadcast-Zellen 0,1,2 = neues Tripel
  wing(&h[0*8], nu.s?1:-1); wing(&h[1*8], nu.p?1:-1); wing(&h[2*8], nu.o?1:-1);
  for(int m=0;m<N;m++){ int b=3+13*m;
    wing(&h[(b+0)*8], store[m].s?1:-1); wing(&h[(b+1)*8], store[m].p?1:-1); wing(&h[(b+2)*8], store[m].o?1:-1);
    for(int kk=3;kk<13;kk++) wing(&h[(b+kk)*8], 1);   // Gatter-Zellen pro Check zurueckgesetzt
  }
  auto ti0=clk::now();
  clEnqueueWriteBuffer(g.q,g.bA,CL_TRUE,0,NC*8*sizeof(double),h,0,nullptr,nullptr);
  *t_io+=ms(ti0,clk::now());
  // --- Settle ---
  cl_mem in=g.bA,out=g.bB;
  auto ts0=clk::now();
  for(int i=0;i<NT;i++){ double t=(double)i*DT; double W=0.225+0.018*std::sin(1.8*t);
    clSetKernelArg(g.k,0,sizeof(cl_mem),&in);clSetKernelArg(g.k,1,sizeof(cl_mem),&out);
    clSetKernelArg(g.k,2,sizeof(cl_mem),&g.bGin);clSetKernelArg(g.k,3,sizeof(cl_mem),&g.bGsg);
    clSetKernelArg(g.k,4,sizeof(cl_mem),&g.bGKw);clSetKernelArg(g.k,5,sizeof(cl_mem),&g.bGb);
    clSetKernelArg(g.k,6,sizeof(int),&NC);clSetKernelArg(g.k,7,sizeof(double),&W);
    size_t gs=((size_t)(NC+63)/64)*64; clEnqueueNDRangeKernel(g.q,g.k,1,nullptr,&gs,nullptr,0,nullptr,nullptr);
    cl_mem tmp=in;in=out;out=tmp;
  }
  clFinish(g.q);
  *t_settle+=ms(ts0,clk::now());
  // --- Bulk-Readback der aktiven Region (in = letzter Out) ---
  auto tr0=clk::now();
  clEnqueueReadBuffer(g.q,in,CL_TRUE,0,NC*8*sizeof(double),h,0,nullptr,nullptr);
  *t_io+=ms(tr0,clk::now());
  std::vector<int> flags(N);
  for(int m=0;m<N;m++) flags[m] = (h[(3+13*m+12)*8+3] > 0) ? 1 : 0;
  return flags;
}

// Strom fahren: store waechst; gibt Endbestand + Protokoll-Zaehler + Timing
struct Run{std::vector<Triple> store; int aufgenommen,verworfen; double t_settle,t_io,wall; int checks;};
static Run strom_lauf(GPU&g,const std::vector<Triple>&strom,const std::vector<Triple>&init,
                      std::vector<std::pair<Triple,int>>*prot=nullptr){
  Run R; R.store=init; R.aufgenommen=0; R.verworfen=0; R.t_settle=0; R.t_io=0; R.checks=0;
  auto w0=clk::now();
  for(auto&t:strom){
    if((int)R.store.size()>=MAX_SLOTS){fprintf(stderr,"MAX_SLOTS!\n");exit(1);}
    std::vector<int> fl=gpu_check(g,t,R.store,&R.t_io,&R.t_settle); if(!R.store.empty()) R.checks++;
    int which=-1; for(size_t m=0;m<fl.size();m++) if(fl[m]){which=(int)m;break;}
    if(which>=0){ R.verworfen++; if(prot) prot->push_back({t,which}); }
    else { R.store.push_back(t); R.aufgenommen++; if(prot) prot->push_back({t,-1}); }
  }
  R.wall=ms(w0,clk::now());
  return R;
}
static const char* TL(const Triple&t){static char b[16];snprintf(b,16,"(%d,%d,%d)",t.s,t.p,t.o);return b;}

int main(){
  GPU g=gpu_init();
  printf("================================================================================\n");
  printf("MINI-OSSIFIKAT-OPT: persistenter Kontext, ganzer Strom in EINER Session\n");
  printf("  resident: Context/Queue/Kernel + bA/bB (NC_max=%d) + Config (alle %d Komparatoren)\n",g.NC_max,MAX_SLOTS);
  printf("  pro Check: aktive Region frisch materialisiert (kein Leck) -> Settle %d -> Bulk-Readback\n",NT);
  printf("================================================================================\n");

  // ---- Szenario A ----
  std::vector<Triple> stromA={{1,1,0},{0,1,1},{1,1,1},{1,0,1},{0,0,0},{0,1,0},{1,0,0},{1,1,0}};
  const char* labA[]={"Himmel-Farbe-blau","Gras-Farbe-gruen","Himmel-Farbe-gruen","Himmel-Zustand-gruen",
                      "Gras-Zustand-blau","Gras-Farbe-blau","Himmel-Zustand-blau","Himmel-Farbe-blau(dup)"};
  std::vector<std::pair<Triple,int>> protA;
  Run rA=strom_lauf(g,stromA,{},&protA);
  printf("\nSZENARIO A (leerer Start):\n");
  for(size_t i=0;i<protA.size();i++)
    printf("    %-24s %-8s %-11s %s\n",labA[i],TL(protA[i].first),protA[i].second<0?"AUFGENOMMEN":"VERWORFEN",
           protA[i].second<0?"-> Slot neu":(std::string("-> Konflikt mit Slot ")+std::to_string(protA[i].second)).c_str());
  auto refA=ref_strom(stromA,{});
  bool eqA=rA.store==refA;
  printf("  Endbestand GPU=%zu Slots, Referenz=%zu | == %s | aufgenommen=%d verworfen=%d\n",
         rA.store.size(),refA.size(),eqA?"JA":"NEIN",rA.aufgenommen,rA.verworfen);

  // ---- Szenario B ----
  std::vector<Triple> initB={{0,1,1}}; std::vector<Triple> stromB={{0,1,0},{1,1,1},{0,1,1}};
  std::vector<std::pair<Triple,int>> protB;
  Run rB=strom_lauf(g,stromB,initB,&protB);
  auto refB=ref_strom(stromB,initB); bool eqB=rB.store==refB;
  printf("\nSZENARIO B (vorgeladen [(0,1,1)]):\n");
  for(auto&p:protB) printf("    %-8s %-11s %s\n",TL(p.first),p.second<0?"AUFGENOMMEN":"VERWORFEN",
           p.second<0?"-> Slot neu":(std::string("-> Konflikt mit Slot ")+std::to_string(p.second)).c_str());
  printf("  Endbestand GPU=%zu, Referenz=%zu | == %s | erster Konflikt gegen Initial-Slot 0: %s\n",
         rB.store.size(),refB.size(),eqB?"JA":"NEIN",(protB[0].second==0)?"JA":"NEIN");

  // ---- Szenario C: Skalierung/Tempo, Endbestand exakt gegen Referenz ----
  printf("\nSZENARIO C (Zufalls-Strom, == Referenz, Tempo):\n");
  printf("  %8s | %11s | %9s | %6s | %9s | %10s | %12s | %12s\n",
         "n_strom","aufgenommen","verworfen","==Ref","wall (s)","ms/Check","ms/Settle","ms/IO");
  unsigned seeds[3]={1050,1200,1500}; int ns[3]={50,200,500};
  for(int si=0;si<3;si++){
    unsigned s=seeds[si]; int n=ns[si]; std::vector<Triple> strom;
    // gleiche Tripel wie Nachtrag 60: numpy default_rng(1000+n).integers(0,2,3) — hier eigener RNG,
    // aber Korrektheit ist intern gegen C++-Referenz definiert (nicht gegen die Python-Sequenz).
    unsigned st=s; auto rnd=[&](){ st=st*1664525u+1013904223u; return (int)((st>>16)&1); };
    for(int i=0;i<n;i++){ Triple t; t.s=rnd();t.p=rnd();t.o=rnd(); strom.push_back(t); }
    Run R=strom_lauf(g,strom,{});
    auto ref=ref_strom(strom,{}); bool eq=R.store==ref;
    printf("  %8d | %11d | %9d | %6s | %9.3f | %10.1f | %12.1f | %12.1f\n",
           n,R.aufgenommen,R.verworfen,eq?"JA":"NEIN",R.wall/1000.0,R.wall/R.checks,
           R.t_settle/R.checks,R.t_io/R.checks);
  }

  // ---- Stabilitaet (Punkt 3): 500-Strom 4x wiederholt, ms/Check darf nicht driften ----
  printf("\nSTABILITAET (kein Leak / Buffer-Wiederverwendung): 500-Strom 4x, ms/Check stabil?\n");
  unsigned st2=4242; auto rnd2=[&](){ st2=st2*1664525u+1013904223u; return (int)((st2>>16)&1); };
  std::vector<Triple> strom500; for(int i=0;i<500;i++){Triple t;t.s=rnd2();t.p=rnd2();t.o=rnd2();strom500.push_back(t);}
  auto ref500=ref_strom(strom500,{});
  for(int rep=0;rep<4;rep++){ Run R=strom_lauf(g,strom500,{}); bool eq=R.store==ref500;
    printf("    Lauf %d: ms/Check=%.1f | wall=%.3fs | ==Ref %s | store=%zu\n",rep+1,R.wall/R.checks,R.wall/1000.0,eq?"JA":"NEIN",R.store.size()); }

  gpu_free(g);
  printf("\nCaveat: reine Geschwindigkeits-Opt (Korrektheit N60 steht); Engine resident statt Subprocess-pro-Check;\n");
  printf("aktive Region pro Check frisch materialisiert (Broadcast+Slot-Daten+Gatter-Zellen) => kein Zustands-Leck;\n");
  printf("SELBER run_gate-Kernel; gegen C++-Referenz (Vorzeichen-Gleichheit); double/fp64; Produktion md5-unberuehrt.\n");
  return 0;
}
