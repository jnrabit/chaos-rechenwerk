// strom_richer.cpp — Mini-Ossifikat reichere Tripel: MEHR-BIT-Felder (B=4 Bit/Feld, 16 Werte) statt 1-Bit.
// gleich(Feld)=AND4 ueber Bit-XNORs (Nachtrag 57); verschieden(O)=NOT(gleich(O))=mind. ein Bit anders (kritisch).
// Konflikt=AND3(gleich(S),gleich(P),verschieden(O)). Resident (Nachtrag 61): Context/Queue/Kernel/Buffer einmal,
// pro Check aktive Region frisch materialisiert (kein Leck). SELBER run_gate-Kernel. double/fp64. md5-unberuehrt.
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
static const double DT=0.005; static const int NT=2000; static const double KW=2.0;
static const int B=4;            // Bit pro Feld
static const int NF=3;           // Felder S,P,O
static const int BC=NF*B;        // Broadcast-Zellen = 12
static const int CPC=53;         // Zellen pro Komparator: 12 Daten + 36 XNOR + 3 gleich + 1 versch + 1 flag
static const int MAX_SLOTS=600;

// Sub-Offsets im Komparator (ab base):
//  0..11   Daten-Anker (gespeichertes Tripel, NF*B Bits)
//  12..47  XNOR-Block: Bit q (0..11) -> OR=12+q*3, NAND=+1, XNOR=+2
//  48,49,50 gleich(S),gleich(P),gleich(O) = AND4 ueber die 4 Bit-XNORs
//  51       verschieden(O) = NOT(gleich(O))
//  52       Konflikt = AND3(gleich(S),gleich(P),versch(O))  <- Flag
static inline int XNOR_CELL(int base,int q){return base+12+q*3+2;}
static const int OFF_GLEICH=48, OFF_VERSCH=51, OFF_FLAG=52;

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
static inline int bit(int v,int b){return (v>>b)&1;}

// Referenz: Konflikt = gleich(S) & gleich(P) & verschieden(O), Felder als ganze Zahlen (unabhaengig vom Gatter-Netz)
static std::vector<Triple> ref_strom(const std::vector<Triple>&strom,const std::vector<Triple>&init){
  std::vector<Triple> b=init;
  for(auto&t:strom){ bool konf=false; for(auto&e:b) if(t.s==e.s&&t.p==e.p&&t.o!=e.o){konf=true;break;}
    if(!konf) b.push_back(t); }
  return b;
}

struct GPU{ cl_context ctx; cl_command_queue q; cl_kernel k; cl_mem bA,bB,bGin,bGsg,bGKw,bGb; std::vector<double> hbuf; int NC_max; };
static void G(std::vector<int>&gin,std::vector<double>&gsg,std::vector<double>&gKw,std::vector<double>&gb,
              int c,std::vector<int>ins,std::vector<double>sgs,double bias){
  for(int j=0;j<4;j++){gin[c*4+j]=-1;gsg[c*4+j]=0;}
  for(size_t j=0;j<ins.size();j++){gin[c*4+j]=ins[j];gsg[c*4+j]=sgs[j];}
  gKw[c]=KW; gb[c]=bias;
}
// Config eines Komparators ab base (Broadcast-Bits liegen bei Zellen 0..BC-1)
static void build_cmp(std::vector<int>&gin,std::vector<double>&gsg,std::vector<double>&gKw,std::vector<double>&gb,int base){
  // Bit-XNORs: q = f*B+b ; OR/NAND lesen Broadcast-Bit (Zelle q) und gespeichertes Bit (base+q)
  for(int q=0;q<BC;q++){ int bc=q, st=base+q; int o=base+12+q*3;
    G(gin,gsg,gKw,gb,o+0,{bc,st},{ 1, 1},+400);   // OR
    G(gin,gsg,gKw,gb,o+1,{bc,st},{-1,-1},+400);   // NAND
    G(gin,gsg,gKw,gb,o+2,{o+0,o+1},{-1,-1},+400); // XNOR = NAND(OR,NAND)
  }
  // gleich(Feld f) = AND4 ueber die 4 Bit-XNORs des Feldes ; Bias -1089 (Fenster (-1452,-726))
  for(int f=0;f<NF;f++){
    std::vector<int> ins; for(int b=0;b<B;b++) ins.push_back(XNOR_CELL(base,f*B+b));
    G(gin,gsg,gKw,gb,base+OFF_GLEICH+f, ins, {1,1,1,1}, -1089.0);
  }
  // verschieden(O) = NOT(gleich(O)) : 1-Eingang-Inverter, Vorzeichen -1, Bias 0
  G(gin,gsg,gKw,gb,base+OFF_VERSCH,{base+OFF_GLEICH+2},{-1},0.0);
  // Konflikt = AND3(gleich(S),gleich(P),verschieden(O)) , Bias -800
  G(gin,gsg,gKw,gb,base+OFF_FLAG,{base+OFF_GLEICH+0,base+OFF_GLEICH+1,base+OFF_VERSCH},{1,1,1},-800.0);
}
static GPU gpu_init(){
  GPU g; cl_int err; cl_uint np=0; clGetPlatformIDs(0,nullptr,&np);
  std::vector<cl_platform_id> ps(np); clGetPlatformIDs(np,ps.data(),nullptr); cl_device_id dev=nullptr;
  for(auto p:ps){char pn[128]={0};clGetPlatformInfo(p,CL_PLATFORM_NAME,sizeof(pn),pn,nullptr);
    cl_device_id d=nullptr; if(clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,1,&d,nullptr)==CL_SUCCESS&&d){
      if(strstr(pn,"AMD Accelerated")){dev=d;break;} if(!dev)dev=d;}}
  char dn[256]={0}; clGetDeviceInfo(dev,CL_DEVICE_NAME,sizeof(dn),dn,nullptr); fprintf(stderr,"Device: %s (resident, B=%d Bit/Feld)\n",dn,B);
  g.ctx=clCreateContext(nullptr,1,&dev,nullptr,nullptr,&err);
  g.q=clCreateCommandQueue(g.ctx,dev,0,&err);
  cl_program prog=clCreateProgramWithSource(g.ctx,1,&kernelSource,nullptr,&err);
  if(clBuildProgram(prog,1,&dev,nullptr,nullptr,nullptr)!=CL_SUCCESS){char l[8192]={0};
    clGetProgramBuildInfo(prog,dev,CL_PROGRAM_BUILD_LOG,sizeof(l),l,nullptr);fprintf(stderr,"BUILD:\n%s\n",l);exit(1);}
  g.k=clCreateKernel(prog,"run_gate",&err);
  g.NC_max=BC+CPC*MAX_SLOTS;
  std::vector<int> gin(g.NC_max*4,-1); std::vector<double> gsg(g.NC_max*4,0),gKw(g.NC_max,0),gb(g.NC_max,0);
  for(int m=0;m<MAX_SLOTS;m++) build_cmp(gin,gsg,gKw,gb, BC+CPC*m);
  g.bA=clCreateBuffer(g.ctx,CL_MEM_READ_WRITE,g.NC_max*8*sizeof(double),nullptr,&err);
  g.bB=clCreateBuffer(g.ctx,CL_MEM_READ_WRITE,g.NC_max*8*sizeof(double),nullptr,&err);
  g.bGin=clCreateBuffer(g.ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gin.size()*sizeof(int),gin.data(),&err);
  g.bGsg=clCreateBuffer(g.ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gsg.size()*sizeof(double),gsg.data(),&err);
  g.bGKw=clCreateBuffer(g.ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gKw.size()*sizeof(double),gKw.data(),&err);
  g.bGb=clCreateBuffer(g.ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gb.size()*sizeof(double),gb.data(),&err);
  g.hbuf.resize(g.NC_max*8);
  return g;
}
static void gpu_free(GPU&g){clReleaseMemObject(g.bA);clReleaseMemObject(g.bB);clReleaseMemObject(g.bGin);
  clReleaseMemObject(g.bGsg);clReleaseMemObject(g.bGKw);clReleaseMemObject(g.bGb);
  clReleaseKernel(g.k);clReleaseCommandQueue(g.q);clReleaseContext(g.ctx);}

// EIN Check: neues Tripel gegen die n_aktiv Slots. Flags (1=Konflikt) je Slot. (settle/io getrennt gemessen)
static std::vector<int> gpu_check(GPU&g,const Triple&nu,const std::vector<Triple>&store,double*t_io,double*t_settle){
  int N=store.size(); if(N==0) return {};
  int NC=BC+CPC*N; double*h=g.hbuf.data();
  // Broadcast-Bits 0..BC-1 = neues Tripel (S-Bits 0..3, P 4..7, O 8..11)
  int nv[3]={nu.s,nu.p,nu.o};
  for(int f=0;f<NF;f++) for(int b=0;b<B;b++) wing(&h[(f*B+b)*8], bit(nv[f],b)?1:-1);
  for(int m=0;m<N;m++){ int base=BC+CPC*m; int sv[3]={store[m].s,store[m].p,store[m].o};
    for(int f=0;f<NF;f++) for(int b=0;b<B;b++) wing(&h[(base+f*B+b)*8], bit(sv[f],b)?1:-1);  // Daten-Anker
    for(int c=12;c<CPC;c++) wing(&h[(base+c)*8], 1);                                          // alle Gatter-Zellen reset
  }
  auto ti0=clk::now();
  clEnqueueWriteBuffer(g.q,g.bA,CL_TRUE,0,NC*8*sizeof(double),h,0,nullptr,nullptr); *t_io+=ms(ti0,clk::now());
  cl_mem in=g.bA,out=g.bB; auto ts0=clk::now();
  for(int i=0;i<NT;i++){ double t=(double)i*DT; double W=0.225+0.018*std::sin(1.8*t);
    clSetKernelArg(g.k,0,sizeof(cl_mem),&in);clSetKernelArg(g.k,1,sizeof(cl_mem),&out);
    clSetKernelArg(g.k,2,sizeof(cl_mem),&g.bGin);clSetKernelArg(g.k,3,sizeof(cl_mem),&g.bGsg);
    clSetKernelArg(g.k,4,sizeof(cl_mem),&g.bGKw);clSetKernelArg(g.k,5,sizeof(cl_mem),&g.bGb);
    clSetKernelArg(g.k,6,sizeof(int),&NC);clSetKernelArg(g.k,7,sizeof(double),&W);
    size_t gs=((size_t)(NC+63)/64)*64; clEnqueueNDRangeKernel(g.q,g.k,1,nullptr,&gs,nullptr,0,nullptr,nullptr);
    cl_mem tmp=in;in=out;out=tmp;
  }
  clFinish(g.q); *t_settle+=ms(ts0,clk::now());
  auto tr0=clk::now();
  clEnqueueReadBuffer(g.q,in,CL_TRUE,0,NC*8*sizeof(double),h,0,nullptr,nullptr); *t_io+=ms(tr0,clk::now());
  std::vector<int> flags(N);
  for(int m=0;m<N;m++) flags[m] = (h[(BC+CPC*m+OFF_FLAG)*8+3] > 0) ? 1 : 0;
  return flags;
}
struct Run{std::vector<Triple> store; int aufgenommen,verworfen; double t_settle,t_io,wall; int checks;};
static Run strom_lauf(GPU&g,const std::vector<Triple>&strom,const std::vector<Triple>&init,
                      std::vector<std::pair<Triple,int>>*prot=nullptr){
  Run R; R.store=init; R.aufgenommen=0;R.verworfen=0;R.t_settle=0;R.t_io=0;R.checks=0; auto w0=clk::now();
  for(auto&t:strom){ if((int)R.store.size()>=MAX_SLOTS){fprintf(stderr,"MAX_SLOTS!\n");exit(1);}
    std::vector<int> fl=gpu_check(g,t,R.store,&R.t_io,&R.t_settle); if(!R.store.empty()) R.checks++;
    int which=-1; for(size_t m=0;m<fl.size();m++) if(fl[m]){which=(int)m;break;}
    if(which>=0){R.verworfen++; if(prot) prot->push_back({t,which});}
    else {R.store.push_back(t);R.aufgenommen++; if(prot) prot->push_back({t,-1});}
  }
  R.wall=ms(w0,clk::now()); return R;
}
static const char* TL(const Triple&t){static char b[24];snprintf(b,24,"(%d,%d,%d)",t.s,t.p,t.o);return b;}

int main(){
  GPU g=gpu_init();
  printf("================================================================================\n");
  printf("MINI-OSSIFIKAT — REICHERE TRIPEL: B=%d Bit/Feld (16 Werte), gleich=AND4(XNOR), versch=NOT(gleich)\n",B);
  printf("  Zellen/Komparator=%d (12 Daten +36 XNOR +3 gleich +1 versch +1 flag), NC_max=%d, Tiefe 5\n",CPC,g.NC_max);
  printf("================================================================================\n");

  // ---- BIT-PROBE: exhaustiv, ein gespeicherter Slot (S=5,P=3,O=9) ----
  double dio=0,dst=0;
  printf("\nBIT-PROBE (exhaustiv gegen Slot (S=5,P=3,O=9)):\n");
  // O-Sweep: S=5,P=3 gleich, O_neu 0..15 -> Konflikt gdw O_neu!=9 (verschieden bei JEDER Bit-Differenz)
  int oerr=0; std::string obad;
  for(int o=0;o<16;o++){ std::vector<int> fl=gpu_check(g,{5,3,o},{{5,3,9}},&dio,&dst);
    int got=fl[0]; int exp=(o!=9)?1:0; if(got!=exp){oerr++; obad+=" "+std::to_string(o);} }
  printf("  O-Sweep (verschieden, inkl. Einzelbit): %d/16 korrekt  %s\n",16-oerr,oerr?("FALSCH bei O="+obad).c_str():"-> verschieden greift bei jedem Bit");
  // S-Sweep: P=3,O=0 (O verschieden), S_neu 0..15 -> Konflikt gdw S_neu==5 (gleich braucht ALLE Bits)
  int serr=0; std::string sbad;
  for(int s=0;s<16;s++){ std::vector<int> fl=gpu_check(g,{s,3,0},{{5,3,9}},&dio,&dst);
    int got=fl[0]; int exp=(s==5)?1:0; if(got!=exp){serr++; sbad+=" "+std::to_string(s);} }
  printf("  S-Sweep (gleich, ALLE Bits noetig):      %d/16 korrekt  %s\n",16-serr,serr?("FALSCH bei S="+sbad).c_str():"-> nur S=5 Konflikt (4/7/13 je 1 Bit daneben: kein Konflikt)");

  // ---- Szenario A ----
  std::vector<Triple> stromA={{0,0,0},{1,0,1},{0,0,1},{0,1,3},{2,0,2},{0,0,4},{8,2,7},{0,1,3},{2,0,0},{1,0,0}};
  const char* labA[]={"Himmel-Farbe-blau","Gras-Farbe-gruen","Himmel-Farbe-gruen","Himmel-Zustand-nass",
                      "Sonne-Farbe-gelb","Himmel-Farbe-rot","Mond-Groesse-gross","Himmel-Zustand-nass(dup)",
                      "Sonne-Farbe-blau","Gras-Farbe-blau"};
  std::vector<std::pair<Triple,int>> protA; Run rA=strom_lauf(g,stromA,{},&protA);
  printf("\nSZENARIO A (leerer Start, lesbare Entitaeten):\n");
  for(size_t i=0;i<protA.size();i++)
    printf("    %-26s %-9s %-11s %s\n",labA[i],TL(protA[i].first),protA[i].second<0?"AUFGENOMMEN":"VERWORFEN",
           protA[i].second<0?"-> Slot neu":(std::string("-> Konflikt mit Slot ")+std::to_string(protA[i].second)).c_str());
  auto refA=ref_strom(stromA,{}); printf("  Endbestand GPU=%zu, Referenz=%zu | == %s | aufgenommen=%d verworfen=%d\n",
         rA.store.size(),refA.size(),(rA.store==refA)?"JA":"NEIN",rA.aufgenommen,rA.verworfen);

  // ---- Szenario B ----
  std::vector<Triple> initB={{0,0,0}}, stromB={{0,0,2},{1,0,1},{0,0,0}};
  std::vector<std::pair<Triple,int>> protB; Run rB=strom_lauf(g,stromB,initB,&protB);
  auto refB=ref_strom(stromB,initB);
  printf("\nSZENARIO B (vorgeladen [(0,0,0)]):  (0,0,2)=O nur Bit1 anders -> Konflikt Initial-Slot 0\n");
  for(auto&p:protB) printf("    %-9s %-11s %s\n",TL(p.first),p.second<0?"AUFGENOMMEN":"VERWORFEN",
           p.second<0?"-> Slot neu":(std::string("-> Konflikt mit Slot ")+std::to_string(p.second)).c_str());
  printf("  Endbestand GPU=%zu, Referenz=%zu | == %s | erster Konflikt gegen Initial-Slot 0: %s\n",
         rB.store.size(),refB.size(),(rB.store==refB)?"JA":"NEIN",(protB[0].second==0)?"JA":"NEIN");

  // ---- Szenario C: Skalierung/Tempo (S,P in 0..3 fuer haeufige Kollisionen, O 0..15) ----
  printf("\nSZENARIO C (Zufalls-Strom, S,P in 0..3 / O in 0..15, == Referenz, Tempo):\n");
  printf("  %8s | %11s | %9s | %6s | %9s | %10s | %12s | %12s\n",
         "n_strom","aufgenommen","verworfen","==Ref","wall (s)","ms/Check","ms/Settle","ms/IO");
  int ns[3]={50,200,500}; unsigned seeds[3]={2050,2200,2500};
  for(int si=0;si<3;si++){ unsigned st=seeds[si]; auto rnd=[&](int m){ st=st*1664525u+1013904223u; return (int)((st>>16)%m); };
    std::vector<Triple> strom; for(int i=0;i<ns[si];i++){ Triple t; t.s=rnd(4); t.p=rnd(4); t.o=rnd(16); strom.push_back(t); }
    Run R=strom_lauf(g,strom,{}); auto ref=ref_strom(strom,{});
    printf("  %8d | %11d | %9d | %6s | %9.3f | %10.1f | %12.1f | %12.1f\n",
           ns[si],R.aufgenommen,R.verworfen,(R.store==ref)?"JA":"NEIN",R.wall/1000.0,R.wall/R.checks,R.t_settle/R.checks,R.t_io/R.checks);
  }

  // ---- Stabilitaet: 500-Strom 3x ----
  printf("\nSTABILITAET (kein Leak): 500-Strom 3x, ms/Check stabil?\n");
  unsigned st2=7777; auto rnd2=[&](int m){ st2=st2*1664525u+1013904223u; return (int)((st2>>16)%m); };
  std::vector<Triple> s500; for(int i=0;i<500;i++){Triple t;t.s=rnd2(4);t.p=rnd2(4);t.o=rnd2(16);s500.push_back(t);}
  auto r500=ref_strom(s500,{});
  for(int rep=0;rep<3;rep++){ Run R=strom_lauf(g,s500,{});
    printf("    Lauf %d: ms/Check=%.1f | wall=%.3fs | ==Ref %s | store=%zu\n",rep+1,R.wall/R.checks,R.wall/1000.0,(R.store==r500)?"JA":"NEIN",R.store.size()); }

  gpu_free(g);
  printf("\nCaveat: Mehr-Bit-Felder (B=4) ueber 1-Bit-Schluessel; gleich=AND4(Bit-XNORs), verschieden=NOT(gleich)=mind. 1 Bit anders;\n");
  printf("Tiefe 5 (Regenerations-Bauregel N57); resident N61 (frisch materialisiert=kein Leck); gegen unabh. Integer-Referenz;\n");
  printf("SELBER run_gate-Kernel; double/fp64; Produktion md5-unberuehrt.\n");
  return 0;
}
