// engine_gpu_red.cpp — N Zellen mean-field auf GPU, GPU-RESIDENT (GPU-Reduktion fuer den mean,
// kein Pro-Tick-Host-Readback). Reine Geschwindigkeits-Optimierung von engine_gpu_mf (Schritt 2).
// Aufruf: ./gpu_red <init.bin> <out.bin> <N> <nticks> <t0> <record0/1>
#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <x86intrin.h>
static const double DT=0.005;
static inline uint64_t raw_jitter(){ uint64_t a=__rdtsc(); volatile float x=0.1f;
  for(int i=0;i<1000;i++) x+=std::sin((float)i); return __rdtsc()-a; }
static inline double acc_jitter(){ uint64_t s=0; for(int j=0;j<64;j++) s+=raw_jitter();
  return ((double)(s%1024)/1024.0 - 0.5); }   // ~8 Bit akkumuliert, zentriert [-0.5,0.5)

const char* kernelSource = R"CLC(
#pragma OPENCL EXTENSION cl_khr_fp64 : enable
typedef struct { double x1,y1,z1,w1,x2,y2,z2,w2; } S8;
static S8 d_add(S8 a,S8 b){S8 r;r.x1=a.x1+b.x1;r.y1=a.y1+b.y1;r.z1=a.z1+b.z1;r.w1=a.w1+b.w1;
 r.x2=a.x2+b.x2;r.y2=a.y2+b.y2;r.z2=a.z2+b.z2;r.w2=a.w2+b.w2;return r;}
static S8 d_sc(double k,S8 a){S8 r;r.x1=k*a.x1;r.y1=k*a.y1;r.z1=k*a.z1;r.w1=k*a.w1;
 r.x2=k*a.x2;r.y2=k*a.y2;r.z2=k*a.z2;r.w2=k*a.w2;return r;}
static S8 deriv(S8 s,double W,double sig,double rho,double bet,double K,double mx1,double mx2){
  S8 d;
  d.x1=sig*(s.y1-s.x1);            d.y1=s.x1*(rho-s.z1)-s.y1+W*s.w1;
  d.z1=s.x1*s.y1-bet*s.z1;         d.w1=-s.w1+s.x1*s.z1-W*s.y1;
  d.x2=sig*(s.y2-s.x2);            d.y2=s.x2*(rho-s.z2)-s.y2+W*s.w2;
  d.z2=s.x2*s.y2-bet*s.z2;         d.w2=-s.w2+s.x2*s.z2-W*s.y2;
  d.x1 += K*(mx1 - s.x1);          d.x2 += K*(mx2 - s.x2);
  return d;
}
static S8 rk4(S8 s,double W,double sig,double rho,double bet,double DT,double K,double mx1,double mx2){
  S8 k1=deriv(s,W,sig,rho,bet,K,mx1,mx2);
  S8 k2=deriv(d_add(s,d_sc(DT/2.0,k1)),W,sig,rho,bet,K,mx1,mx2);
  S8 k3=deriv(d_add(s,d_sc(DT/2.0,k2)),W,sig,rho,bet,K,mx1,mx2);
  S8 k4=deriv(d_add(s,d_sc(DT,    k3)),W,sig,rho,bet,K,mx1,mx2);
  return d_add(s, d_sc(DT/6.0, d_add(k1, d_add(d_sc(2.0,k2), d_add(d_sc(2.0,k3), k4)))));
}
// Reduktion: eine Work-Group, Grid-Stride + Baum-Reduktion -> mean.x1/x2 im GPU-Buffer
__kernel void reduce_mean(__global const double* state, int N, __global double* meanbuf,
                          __local double* lx1, __local double* lx2){
  int lid=get_local_id(0), lsz=get_local_size(0);
  double s1=0.0, s2=0.0;
  for(int n=lid; n<N; n+=lsz){ s1+=state[n*8+0]; s2+=state[n*8+4]; }
  lx1[lid]=s1; lx2[lid]=s2; barrier(CLK_LOCAL_MEM_FENCE);
  for(int off=lsz/2; off>0; off>>=1){
    if(lid<off){ lx1[lid]+=lx1[lid+off]; lx2[lid]+=lx2[lid+off]; }
    barrier(CLK_LOCAL_MEM_FENCE);
  }
  if(lid==0){ meanbuf[0]=lx1[0]/(double)N; meanbuf[1]=lx2[0]/(double)N; }
}
__kernel void run_tick(__global const double* in,__global double* out,
                       __global const double* meanbuf,int N,double W){
  int g=get_global_id(0); if(g>=N) return;
  const double SIG=10.0,RHO=28.0,BET=8.0/3.0,DTk=0.005,K=0.05;
  double mx1=meanbuf[0], mx2=meanbuf[1];
  S8 s; s.x1=in[g*8+0];s.y1=in[g*8+1];s.z1=in[g*8+2];s.w1=in[g*8+3];
        s.x2=in[g*8+4];s.y2=in[g*8+5];s.z2=in[g*8+6];s.w2=in[g*8+7];
  s = rk4(s,W,SIG,RHO,BET,DTk,K,mx1,mx2);
  out[g*8+0]=s.x1;out[g*8+1]=s.y1;out[g*8+2]=s.z1;out[g*8+3]=s.w1;
  out[g*8+4]=s.x2;out[g*8+5]=s.y2;out[g*8+6]=s.z2;out[g*8+7]=s.w2;
}
)CLC";

static std::vector<double> read_bin(const char* p){
  std::ifstream f(p,std::ios::binary|std::ios::ate); size_t n=f.tellg(); f.seekg(0);
  std::vector<double> v(n/sizeof(double)); f.read((char*)v.data(),n); return v; }

int main(int argc,char**argv){
  if(argc<7){fprintf(stderr,"usage: %s init.bin out.bin N nticks t0 record\n",argv[0]);return 2;}
  int N=atoi(argv[3]), nticks=atoi(argv[4]); double t0=atof(argv[5]); int record=atoi(argv[6]);
  cl_int err; cl_uint np=0; clGetPlatformIDs(0,nullptr,&np);
  std::vector<cl_platform_id> ps(np); clGetPlatformIDs(np,ps.data(),nullptr);
  cl_device_id dev=nullptr;
  for(auto p:ps){char pn[128]={0};clGetPlatformInfo(p,CL_PLATFORM_NAME,sizeof(pn),pn,nullptr);
    cl_device_id d=nullptr; if(clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,1,&d,nullptr)==CL_SUCCESS&&d){
      if(strstr(pn,"AMD Accelerated")){dev=d;break;} if(!dev)dev=d;}}
  if(!dev){fprintf(stderr,"no GPU\n");return 1;}
  char dn[256]={0}; clGetDeviceInfo(dev,CL_DEVICE_NAME,sizeof(dn),dn,nullptr); fprintf(stderr,"Device: %s\n",dn);
  cl_context ctx=clCreateContext(nullptr,1,&dev,nullptr,nullptr,&err);
  cl_command_queue q=clCreateCommandQueue(ctx,dev,0,&err);
  cl_program prog=clCreateProgramWithSource(ctx,1,&kernelSource,nullptr,&err);
  if(clBuildProgram(prog,1,&dev,nullptr,nullptr,nullptr)!=CL_SUCCESS){char l[8192]={0};
    clGetProgramBuildInfo(prog,dev,CL_PROGRAM_BUILD_LOG,sizeof(l),l,nullptr);fprintf(stderr,"BUILD:\n%s\n",l);return 1;}
  cl_kernel kred=clCreateKernel(prog,"reduce_mean",&err);
  cl_kernel kint=clCreateKernel(prog,"run_tick",&err);

  std::vector<double> init=read_bin(argv[1]);
  std::vector<double> warr=(argc>7&&argv[7][0]!='-')? read_bin(argv[7]) : std::vector<double>();
  double jscale=(argc>8)? atof(argv[8]) : 0.0;
  cl_mem bA=clCreateBuffer(ctx,CL_MEM_READ_WRITE|CL_MEM_COPY_HOST_PTR,N*8*sizeof(double),init.data(),&err);
  cl_mem bB=clCreateBuffer(ctx,CL_MEM_READ_WRITE,N*8*sizeof(double),nullptr,&err);
  cl_mem bM=clCreateBuffer(ctx,CL_MEM_READ_WRITE,2*sizeof(double),nullptr,&err);
  const int LSZ=256;
  std::vector<double> full; if(record) full.assign((size_t)N*nticks*8,0.0);
  std::vector<double> st(N*8);

  cl_mem in=bA,out=bB;
  auto T0=std::chrono::high_resolution_clock::now();
  for(int i=0;i<nticks;i++){
    double t=t0+(double)i*DT; double W=(i<(int)warr.size())? warr[i] : (0.225+0.018*std::sin(1.8*t)); if(jscale!=0.0) W+=acc_jitter()*jscale;
    // reduce_mean(in) -> bM
    clSetKernelArg(kred,0,sizeof(cl_mem),&in); clSetKernelArg(kred,1,sizeof(int),&N);
    clSetKernelArg(kred,2,sizeof(cl_mem),&bM);
    clSetKernelArg(kred,3,LSZ*sizeof(double),nullptr); clSetKernelArg(kred,4,LSZ*sizeof(double),nullptr);
    size_t gred=LSZ, lred=LSZ;
    clEnqueueNDRangeKernel(q,kred,1,nullptr,&gred,&lred,0,nullptr,nullptr);
    // run_tick(in, out, bM)
    clSetKernelArg(kint,0,sizeof(cl_mem),&in); clSetKernelArg(kint,1,sizeof(cl_mem),&out);
    clSetKernelArg(kint,2,sizeof(cl_mem),&bM); clSetKernelArg(kint,3,sizeof(int),&N); clSetKernelArg(kint,4,sizeof(double),&W);
    size_t g=((size_t)(N+63)/64)*64;
    clEnqueueNDRangeKernel(q,kint,1,nullptr,&g,nullptr,0,nullptr,nullptr);
    if(record){ clEnqueueReadBuffer(q,out,CL_TRUE,0,N*8*sizeof(double),st.data(),0,nullptr,nullptr);
      for(int n=0;n<N;n++) for(int c=0;c<8;c++) full[((size_t)n*nticks+i)*8+c]=st[n*8+c]; }
    cl_mem tmp=in; in=out; out=tmp;     // nur Pointer-Swap, kein Transfer
  }
  clFinish(q);
  auto T1=std::chrono::high_resolution_clock::now();
  double ms=std::chrono::duration_cast<std::chrono::microseconds>(T1-T0).count()/1000.0;
  fprintf(stderr,"GPU-RED N=%d nticks=%d: %.2f ms (%.0f ticks/s)\n",N,nticks,ms,nticks/(ms/1000.0));

  if(record){ std::ofstream of(argv[2],std::ios::binary); of.write((char*)full.data(),full.size()*sizeof(double)); }
  clReleaseMemObject(bA);clReleaseMemObject(bB);clReleaseMemObject(bM);
  clReleaseKernel(kred);clReleaseKernel(kint);clReleaseProgram(prog);clReleaseCommandQueue(q);clReleaseContext(ctx);
  return 0;
}
