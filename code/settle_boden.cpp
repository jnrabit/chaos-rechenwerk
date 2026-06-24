// settle_boden.cpp — Mini-Ossifikat-Opt: Settle-Boden messen. Wie viele Ticks braucht das TIEFSTE
// (Mehr-Bit-Konflikt-, Tiefe 5) Netz, bis das FLAG bit-exakt == 2000-Tick-Wahrheit ist UND stabil bleibt?
// Messgroesse = Flag-Vorzeichen vs 2000-Tick-Referenz (nicht Augenschein) + Sattheit aller Zwischenstufen.
// SELBER run_gate-Kernel + dieselbe Mehr-Bit-Logik wie strom_richer.cpp (Nachtrag 62). md5-unberuehrt.
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
static const double DT=0.005; static const double KW=2.0;
static const int B=4, NF=3, BC=NF*B, CPC=53;
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

struct Triple{int s,p,o;};
static void wing(double*c,int sgn){double a=sgn*9.6,w=sgn*363.0; c[0]=a;c[1]=a;c[2]=25;c[3]=w;c[4]=a;c[5]=a;c[6]=25;c[7]=w;}
static inline int bit(int v,int b){return (v>>b)&1;}
static void G(std::vector<int>&gin,std::vector<double>&gsg,std::vector<double>&gKw,std::vector<double>&gb,
              int c,std::vector<int>ins,std::vector<double>sgs,double bias){
  for(int j=0;j<4;j++){gin[c*4+j]=-1;gsg[c*4+j]=0;}
  for(size_t j=0;j<ins.size();j++){gin[c*4+j]=ins[j];gsg[c*4+j]=sgs[j];}
  gKw[c]=KW; gb[c]=bias;
}
static void build_cmp(std::vector<int>&gin,std::vector<double>&gsg,std::vector<double>&gKw,std::vector<double>&gb,int base){
  for(int q=0;q<BC;q++){ int bc=q, st=base+q; int o=base+12+q*3;
    G(gin,gsg,gKw,gb,o+0,{bc,st},{ 1, 1},+400);
    G(gin,gsg,gKw,gb,o+1,{bc,st},{-1,-1},+400);
    G(gin,gsg,gKw,gb,o+2,{o+0,o+1},{-1,-1},+400);
  }
  for(int f=0;f<NF;f++){ std::vector<int> ins; for(int b=0;b<B;b++) ins.push_back(XNOR_CELL(base,f*B+b));
    G(gin,gsg,gKw,gb,base+OFF_GLEICH+f, ins, {1,1,1,1}, -1089.0); }
  G(gin,gsg,gKw,gb,base+OFF_VERSCH,{base+OFF_GLEICH+2},{-1},0.0);
  G(gin,gsg,gKw,gb,base+OFF_FLAG,{base+OFF_GLEICH+0,base+OFF_GLEICH+1,base+OFF_VERSCH},{1,1,1},-800.0);
}

// EIN Komparator (BC + CPC Zellen). Settle NT Ticks, gib Flag-Vorzeichen + min|w1| ueber alle 41 Gatter-Zellen.
struct Res{int flag; double minabs; double minabs_and3;};
struct GPU{cl_context ctx;cl_command_queue q;cl_kernel k;cl_mem bA,bB,bGin,bGsg,bGKw,bGb;int NC;};
static GPU G_;
static Res run_case(const Triple&nu,const Triple&st,int NT,double*t_settle){
  int NC=G_.NC; std::vector<double> h(NC*8,0.0);
  int nv[3]={nu.s,nu.p,nu.o}, sv[3]={st.s,st.p,st.o};
  for(int f=0;f<NF;f++)for(int b=0;b<B;b++) wing(&h[(f*B+b)*8], bit(nv[f],b)?1:-1);   // Broadcast
  int base=BC;
  for(int f=0;f<NF;f++)for(int b=0;b<B;b++) wing(&h[(base+f*B+b)*8], bit(sv[f],b)?1:-1); // Daten
  for(int c=12;c<CPC;c++) wing(&h[(base+c)*8], 1);                                       // Gatter-Zellen reset
  clEnqueueWriteBuffer(G_.q,G_.bA,CL_TRUE,0,NC*8*sizeof(double),h.data(),0,nullptr,nullptr);
  cl_mem in=G_.bA,out=G_.bB; auto ts0=clk::now();
  for(int i=0;i<NT;i++){ double t=(double)i*DT; double W=0.225+0.018*std::sin(1.8*t);
    clSetKernelArg(G_.k,0,sizeof(cl_mem),&in);clSetKernelArg(G_.k,1,sizeof(cl_mem),&out);
    clSetKernelArg(G_.k,2,sizeof(cl_mem),&G_.bGin);clSetKernelArg(G_.k,3,sizeof(cl_mem),&G_.bGsg);
    clSetKernelArg(G_.k,4,sizeof(cl_mem),&G_.bGKw);clSetKernelArg(G_.k,5,sizeof(cl_mem),&G_.bGb);
    clSetKernelArg(G_.k,6,sizeof(int),&NC);clSetKernelArg(G_.k,7,sizeof(double),&W);
    size_t gs=((size_t)(NC+63)/64)*64; clEnqueueNDRangeKernel(G_.q,G_.k,1,nullptr,&gs,nullptr,0,nullptr,nullptr);
    cl_mem tmp=in;in=out;out=tmp;
  }
  clFinish(G_.q); if(t_settle)*t_settle+=ms(ts0,clk::now());
  clEnqueueReadBuffer(G_.q,in,CL_TRUE,0,NC*8*sizeof(double),h.data(),0,nullptr,nullptr);
  Res r; r.flag = (h[(base+OFF_FLAG)*8+3]>0)?1:0;
  r.minabs=1e9; for(int c=12;c<CPC;c++){ double a=std::fabs(h[(base+c)*8+3]); if(a<r.minabs)r.minabs=a; }
  r.minabs_and3=std::fabs(h[(base+OFF_FLAG)*8+3]);
  return r;
}

int main(){
  cl_int err; cl_uint np=0; clGetPlatformIDs(0,nullptr,&np);
  std::vector<cl_platform_id> ps(np); clGetPlatformIDs(np,ps.data(),nullptr); cl_device_id dev=nullptr;
  for(auto p:ps){char pn[128]={0};clGetPlatformInfo(p,CL_PLATFORM_NAME,sizeof(pn),pn,nullptr);
    cl_device_id d=nullptr; if(clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,1,&d,nullptr)==CL_SUCCESS&&d){
      if(strstr(pn,"AMD Accelerated")){dev=d;break;} if(!dev)dev=d;}}
  char dn[256]={0};clGetDeviceInfo(dev,CL_DEVICE_NAME,sizeof(dn),dn,nullptr);fprintf(stderr,"Device: %s\n",dn);
  G_.ctx=clCreateContext(nullptr,1,&dev,nullptr,nullptr,&err); G_.q=clCreateCommandQueue(G_.ctx,dev,0,&err);
  cl_program prog=clCreateProgramWithSource(G_.ctx,1,&kernelSource,nullptr,&err);
  if(clBuildProgram(prog,1,&dev,nullptr,nullptr,nullptr)!=CL_SUCCESS){char l[8192]={0};
    clGetProgramBuildInfo(prog,dev,CL_PROGRAM_BUILD_LOG,sizeof(l),l,nullptr);fprintf(stderr,"%s\n",l);return 1;}
  G_.k=clCreateKernel(prog,"run_gate",&err);
  G_.NC=BC+CPC*1;   // EIN Komparator (tiefstes Netz, Tiefe 5)
  std::vector<int> gin(G_.NC*4,-1); std::vector<double> gsg(G_.NC*4,0),gKw(G_.NC,0),gb(G_.NC,0);
  build_cmp(gin,gsg,gKw,gb,BC);
  G_.bA=clCreateBuffer(G_.ctx,CL_MEM_READ_WRITE,G_.NC*8*sizeof(double),nullptr,&err);
  G_.bB=clCreateBuffer(G_.ctx,CL_MEM_READ_WRITE,G_.NC*8*sizeof(double),nullptr,&err);
  G_.bGin=clCreateBuffer(G_.ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gin.size()*sizeof(int),gin.data(),&err);
  G_.bGsg=clCreateBuffer(G_.ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gsg.size()*sizeof(double),gsg.data(),&err);
  G_.bGKw=clCreateBuffer(G_.ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gKw.size()*sizeof(double),gKw.data(),&err);
  G_.bGb=clCreateBuffer(G_.ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gb.size()*sizeof(double),gb.data(),&err);

  struct Case{const char*name;Triple nu,st;int hard;};
  std::vector<Case> cases={
    {"L  Konflikt O grob versch (mehrere Bit)", {5,3,9},{5,3,0},0},
    {"L- Nicht-Konflikt S grob versch",         {10,3,0},{5,3,9},0},
    {"H1 Konflikt O nur Bit0  (9 vs 8)",         {5,3,8},{5,3,9},1},
    {"H2 Konflikt O nur Bit3  (9 vs 1)",         {5,3,1},{5,3,9},1},
    {"H3 Nicht-Konflikt S nur Bit0 (5 vs 4),O=", {4,3,9},{5,3,9},1},
    {"H4 Szen-A Einzelbit O (0,0,1)v(0,0,0)",    {0,0,1},{0,0,0},1},
  };
  std::vector<int> NTs={100,250,500,750,1000,1250,1500,2000};

  printf("================================================================================\n");
  printf("SETTLE-BODEN: ab welcher Tick-Zahl ist das FLAG bit-exakt == 2000-Tick-Wahrheit + stabil?\n");
  printf("  tiefstes Netz: EIN Mehr-Bit-Konflikt-Komparator (Tiefe 5, 53 Zellen). H*=Einzelbit-Faelle.\n");
  printf("================================================================================\n");

  // 2000-Tick-Wahrheit je Fall
  std::vector<int> truth(cases.size());
  for(size_t c=0;c<cases.size();c++){ Res r=run_case(cases[c].nu,cases[c].st,2000,nullptr); truth[c]=r.flag; }

  // Tabelle: pro Fall ueber alle NT — Flag, korrekt?, min|w1| Zwischenstufen, |AND3|
  std::vector<int> tmin(cases.size(),-1);
  for(size_t c=0;c<cases.size();c++){
    printf("\n%-44s  (2000-Tick-Wahrheit Flag=%d)\n",cases[c].name,truth[c]);
    printf("   %5s | %4s | %7s | %8s | %9s | %s\n","NT","Flag","korrekt","min|w1|","|AND3|","satt(alle>300)");
    // erst alle NT messen, dann T_min = kleinstes NT ab dem ALLE >= ihm korrekt sind
    std::vector<int> ok(NTs.size());
    for(size_t i=0;i<NTs.size();i++){ Res r=run_case(cases[c].nu,cases[c].st,NTs[i],nullptr);
      ok[i]=(r.flag==truth[c]);
      printf("   %5d | %4d | %7s | %8.1f | %9.1f | %s\n",NTs[i],r.flag,ok[i]?"JA":"NEIN",
             r.minabs,r.minabs_and3,(r.minabs>300)?"satt":"verwaschen");
    }
    // T_min = kleinstes NT, ab dem ok[i] und ALLE groesseren ok sind (stabile Korrektheit)
    for(int i=(int)NTs.size()-1;i>=0;i--){ if(ok[i]) tmin[c]=NTs[i]; else break; }
    printf("   -> T_min (bit-exakt UND ab da stabil) = %d Ticks\n",tmin[c]);
  }

  int boden=0,boden_hard=0,boden_leicht=0;
  for(size_t c=0;c<cases.size();c++){ if(tmin[c]>boden)boden=tmin[c];
    if(cases[c].hard){ if(tmin[c]>boden_hard)boden_hard=tmin[c]; } else { if(tmin[c]>boden_leicht)boden_leicht=tmin[c]; } }
  printf("\n================================================================================\n");
  printf("BODEN = max T_min ueber alle Faelle = %d Ticks | leichte=%d  schwere(Tiefe-5/Einzelbit)=%d\n",boden,boden_leicht,boden_hard);
  printf("Tiefe erzwingt hoeheren Boden? %s (schwer %d vs leicht %d)\n",(boden_hard>boden_leicht)?"JA":"NEIN",boden_hard,boden_leicht);

  // Empfehlung: naechster Tabellenpunkt UEBER dem Boden (Sicherheitsmarge), sonst 2000
  int emp=2000; for(int nt:NTs) if(nt>boden){emp=nt;break;}
  // ms/Check messen: warmlaufen, dann je 30 Checks bei emp und bei 2000
  double t_emp=0,t_2k=0;
  for(int w=0;w<3;w++) run_case(cases[0].nu,cases[0].st,emp,nullptr);
  for(int r=0;r<30;r++) run_case(cases[2].nu,cases[2].st,emp,&t_emp);
  for(int r=0;r<30;r++) run_case(cases[2].nu,cases[2].st,2000,&t_2k);
  printf("\nEmpfehlung (Boden + Sicherheitsmarge) = %d Ticks\n",emp);
  printf("  ms/Check @%d = %.2f ms | @2000 = %.2f ms | Ersparnis %.2fx\n",emp,t_emp/30.0,t_2k/30.0,(t_2k/30.0)/(t_emp/30.0));

  clReleaseMemObject(G_.bA);clReleaseMemObject(G_.bB);clReleaseMemObject(G_.bGin);clReleaseMemObject(G_.bGsg);
  clReleaseMemObject(G_.bGKw);clReleaseMemObject(G_.bGb);clReleaseKernel(G_.k);clReleaseCommandQueue(G_.q);clReleaseContext(G_.ctx);
  printf("\nCaveat: reine Settle-Boden-Messung (Korrektheit N62 steht); Messgroesse=Flag bit-exakt vs 2000-Tick-Referenz +\n");
  printf("Stabilitaet ab T_min; tiefstes Netz (Tiefe 5) inkl. Einzelbit-O/S-Faelle; SELBER Kernel+Logik nur frueher gestoppt;\n");
  printf("kein Wert ohne bit-exakte Stabilitaet gesenkt; double/fp64; Produktion md5-unberuehrt.\n");
  return 0;
}
