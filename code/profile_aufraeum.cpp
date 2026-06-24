// profile_aufraeum.cpp — Aufraeum-Vorfrage: lohnen Bulk-Readback / multi-WG ueberhaupt? Die richtige NULL.
// Reine Profil-Messung, NICHTS gebaut/optimiert. Resident Mehr-Bit-Konflikt-Netz (N61/62), SELBER run_gate-Kernel.
// (1) Readback-Skalierung: gebuendelt (1 Transfer) vs per-Flag (N Reads) ueber wachsendes N.
// (2) Settle-Flachheit: ms/Settle vs N -> wo saettigt die GPU (WG-Kante)? double/fp64. md5-unberuehrt.
#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
using clk=std::chrono::high_resolution_clock;
static double ms(clk::time_point a,clk::time_point b){return std::chrono::duration_cast<std::chrono::microseconds>(b-a).count()/1000.0;}
static const double DT=0.005; static const double KW=2.0;
static const int B=4, NF=3, BC=NF*B, CPC=53;
static const int OFF_FLAG=52;
static inline int XNOR_CELL(int base,int q){return base+12+q*3+2;}
static const int OFF_GLEICH=48, OFF_VERSCH=51;
static const int MAX_SLOTS=16384;
static const int NT=750;   // neuer Settle-Default (Nachtrag 63)

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
static double median3(double a,double b,double c){return std::max(std::min(a,b),std::min(std::max(a,b),c));}

cl_context ctx; cl_command_queue Q; cl_kernel K; cl_mem bA,bB,bGin,bGsg,bGKw,bGb;
static void settle_once(int NC,double*t_settle){
  cl_mem in=bA,out=bB; auto ts0=clk::now();
  for(int i=0;i<NT;i++){ double t=(double)i*DT; double W=0.225+0.018*std::sin(1.8*t);
    clSetKernelArg(K,0,sizeof(cl_mem),&in);clSetKernelArg(K,1,sizeof(cl_mem),&out);
    clSetKernelArg(K,2,sizeof(cl_mem),&bGin);clSetKernelArg(K,3,sizeof(cl_mem),&bGsg);
    clSetKernelArg(K,4,sizeof(cl_mem),&bGKw);clSetKernelArg(K,5,sizeof(cl_mem),&bGb);
    clSetKernelArg(K,6,sizeof(int),&NC);clSetKernelArg(K,7,sizeof(double),&W);
    size_t gs=((size_t)(NC+63)/64)*64; clEnqueueNDRangeKernel(Q,K,1,nullptr,&gs,nullptr,0,nullptr,nullptr);
    cl_mem tmp=in;in=out;out=tmp;
  }
  clFinish(Q); if(t_settle)*t_settle=ms(ts0,clk::now());
  // 'in' zeigt nach geradem NT auf bA; egal, beide Buffer halten Resultat-Region
}

int main(){
  cl_int err; cl_uint np=0; clGetPlatformIDs(0,nullptr,&np);
  std::vector<cl_platform_id> ps(np); clGetPlatformIDs(np,ps.data(),nullptr); cl_device_id dev=nullptr;
  for(auto p:ps){char pn[128]={0};clGetPlatformInfo(p,CL_PLATFORM_NAME,sizeof(pn),pn,nullptr);
    cl_device_id d=nullptr; if(clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,1,&d,nullptr)==CL_SUCCESS&&d){
      if(strstr(pn,"AMD Accelerated")){dev=d;break;} if(!dev)dev=d;}}
  char dn[256]={0};clGetDeviceInfo(dev,CL_DEVICE_NAME,sizeof(dn),dn,nullptr);
  cl_ulong gmem=0; clGetDeviceInfo(dev,CL_DEVICE_GLOBAL_MEM_SIZE,sizeof(gmem),&gmem,nullptr);
  cl_uint cu=0; clGetDeviceInfo(dev,CL_DEVICE_MAX_COMPUTE_UNITS,sizeof(cu),&cu,nullptr);
  fprintf(stderr,"Device: %s | %llu MB | %u CUs\n",dn,(unsigned long long)(gmem/1024/1024),cu);
  ctx=clCreateContext(nullptr,1,&dev,nullptr,nullptr,&err); Q=clCreateCommandQueue(ctx,dev,0,&err);
  cl_program prog=clCreateProgramWithSource(ctx,1,&kernelSource,nullptr,&err);
  if(clBuildProgram(prog,1,&dev,nullptr,nullptr,nullptr)!=CL_SUCCESS){char l[8192]={0};
    clGetProgramBuildInfo(prog,dev,CL_PROGRAM_BUILD_LOG,sizeof(l),l,nullptr);fprintf(stderr,"%s\n",l);return 1;}
  K=clCreateKernel(prog,"run_gate",&err);

  int NC_max=BC+CPC*MAX_SLOTS;
  fprintf(stderr,"NC_max=%d (~%.0f MB je Buffer, 2 Buffer + Config)\n",NC_max,(double)NC_max*8*8/1e6);
  std::vector<int> gin(NC_max*4,-1); std::vector<double> gsg(NC_max*4,0),gKw(NC_max,0),gb(NC_max,0);
  for(int m=0;m<MAX_SLOTS;m++) build_cmp(gin,gsg,gKw,gb,BC+CPC*m);
  bA=clCreateBuffer(ctx,CL_MEM_READ_WRITE,(size_t)NC_max*8*sizeof(double),nullptr,&err);
  bB=clCreateBuffer(ctx,CL_MEM_READ_WRITE,(size_t)NC_max*8*sizeof(double),nullptr,&err);
  bGin=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gin.size()*sizeof(int),gin.data(),&err);
  bGsg=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gsg.size()*sizeof(double),gsg.data(),&err);
  bGKw=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gKw.size()*sizeof(double),gKw.data(),&err);
  bGb=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gb.size()*sizeof(double),gb.data(),&err);
  if(err!=CL_SUCCESS){fprintf(stderr,"Buffer-Allok-Fehler %d\n",err);return 1;}
  std::vector<double> h((size_t)NC_max*8,0.0);

  auto materialize=[&](int N){
    for(int f=0;f<NF;f++)for(int b=0;b<B;b++) wing(&h[(f*B+b)*8], (f==2&&b==0)?1:-1);   // Broadcast = irgendein Tripel
    for(int m=0;m<N;m++){ int base=BC+CPC*m;
      for(int c=0;c<12;c++) wing(&h[(base+c)*8], (m+c)&1?1:-1);
      for(int c=12;c<CPC;c++) wing(&h[(base+c)*8], 1);
    }
    clEnqueueWriteBuffer(Q,bA,CL_TRUE,0,(size_t)(BC+CPC*N)*8*sizeof(double),h.data(),0,nullptr,nullptr);
  };
  // Readback gebuendelt: ein Transfer der aktiven Region
  auto rb_bulk=[&](int N)->double{ int NC=BC+CPC*N; auto t0=clk::now();
    clEnqueueReadBuffer(Q,bA,CL_TRUE,0,(size_t)NC*8*sizeof(double),h.data(),0,nullptr,nullptr);
    return ms(t0,clk::now()); };
  // Readback per-Flag: N einzelne blockierende Reads je Flag-Zelle
  auto rb_perflag=[&](int N)->double{ double w; auto t0=clk::now();
    for(int m=0;m<N;m++) clEnqueueReadBuffer(Q,bA,CL_TRUE,(size_t)((BC+CPC*m+OFF_FLAG)*8+3)*sizeof(double),sizeof(double),&w,0,nullptr,nullptr);
    return ms(t0,clk::now()); };

  printf("================================================================================\n");
  printf("AUFRAEUM-VORFRAGE: lohnen Bulk-Readback / multi-WG? (die richtige Null, nichts gebaut)\n");
  printf("  Mehr-Bit-Konflikt-Netz, NC=12+53*N, Settle NT=%d, gfx1100 %u CUs\n",NT,cu);
  printf("================================================================================\n");

  // ---- (1)+(2) kombinierte Skalierungstabelle ----
  printf("\n%8s | %9s | %11s | %13s | %14s | %12s\n",
         "N","NC","Settle(ms)","Settle/1k Zell","RB gebuendelt","RB per-Flag");
  std::vector<int> Ns={32,100,500,1000,2000,4096,8192,16384};
  std::vector<double> set_per1k;
  for(int N:Ns){
    int NC=BC+CPC*N;
    materialize(N);
    double s1,s2,s3; settle_once(NC,&s1); settle_once(NC,&s2); settle_once(NC,&s3);
    double set=median3(s1,s2,s3);
    double bulk=median3(rb_bulk(N),rb_bulk(N),rb_bulk(N));
    double perflag = (N<=1000)? median3(rb_perflag(N),rb_perflag(N),rb_perflag(N)) : -1;
    double per1k = set/(NC/1000.0);
    set_per1k.push_back(per1k);
    char pf[32]; if(perflag<0) snprintf(pf,32,"(uebersprungen)"); else snprintf(pf,32,"%.2f ms",perflag);
    printf("%8d | %9d | %11.2f | %13.3f | %11.2f ms | %12s\n",N,NC,set,per1k,bulk,pf);
  }

  // ---- Flachheits-Kante: ab wo waechst Settle/1k NICHT mehr (= linear mit NC, GPU gesaettigt) ----
  printf("\nFlachheit (Settle/1k Zellen faellt = noch latenz-gebunden/flach; flacht aus = GPU gesaettigt):\n");
  for(size_t i=1;i<Ns.size();i++){
    double drop = set_per1k[i-1]>0 ? (set_per1k[i]/set_per1k[i-1]) : 0;
    const char* tag = (drop>0.85)? "<- Settle/1k ~konstant => GPU SAETTIGT (NC dominiert)" : "faellt noch (flach, latenz-gebunden)";
    printf("   N %5d->%5d : Settle/1k %.3f->%.3f  (x%.2f)  %s\n",Ns[i-1],Ns[i],set_per1k[i-1],set_per1k[i],drop,tag);
  }

  clReleaseMemObject(bA);clReleaseMemObject(bB);clReleaseMemObject(bGin);clReleaseMemObject(bGsg);
  clReleaseMemObject(bGKw);clReleaseMemObject(bGb);clReleaseKernel(K);clReleaseCommandQueue(Q);clReleaseContext(ctx);
  printf("\nCaveat: reine Profil-Messung (die richtige Null vor Optimierung), NICHTS gebaut; SELBER run_gate-Kernel +\n");
  printf("Mehr-Bit-Logik (N61/62); RB gebuendelt = heutiger residenter Strom, RB per-Flag = Kontrast/N58-Artefakt;\n");
  printf("Settle/1k flach = GPU nicht gesaettigt (multi-WG bringt nichts); double/fp64; Produktion md5-unberuehrt.\n");
  return 0;
}
