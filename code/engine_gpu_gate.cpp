// engine_gpu_gate.cpp — erster LOGIK-Baustein auf GPU: das Gatter-Primitiv (sign(NET)*363-Regeneration).
// Jede Zelle: isolierte Doppelkern-RK4 + Gatter-Term d.w1+=gK*(sign(NET)*363-w1). Konfig-Buffer pro Zelle.
// Aufruf: ./gpu_gate init.bin out.bin N nticks t0 gin.bin gsg.bin gKw.bin gbias.bin
#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>
#include <chrono>
#include <cmath>
static const double DT=0.005;

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
  if(gk!=0.0) d.w1 += gk*(gnet - s.w1);   // Gatter-Term (mean-field-Muster, Ziel=sign(NET)*363)
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
  if(gk!=0.0){                                  // NET aus dem Tick-Anfangs-Schnappschuss (in)
    double net=gbias[g];
    for(int j=0;j<4;j++){ int idx=gin[g*4+j]; if(idx>=0) net += gsg[g*4+j]*in[idx*8+3]; }
    gnet=(net>=0.0?363.0:-363.0);               // Regeneration auf den Anker
  }
  S8 s; s.x1=in[g*8+0];s.y1=in[g*8+1];s.z1=in[g*8+2];s.w1=in[g*8+3];
        s.x2=in[g*8+4];s.y2=in[g*8+5];s.z2=in[g*8+6];s.w2=in[g*8+7];
  s=rk4_g(s,W,SIG,RHO,BET,DTk,gk,gnet);
  out[g*8+0]=s.x1;out[g*8+1]=s.y1;out[g*8+2]=s.z1;out[g*8+3]=s.w1;
  out[g*8+4]=s.x2;out[g*8+5]=s.y2;out[g*8+6]=s.z2;out[g*8+7]=s.w2;
}
)CLC";

template<class T> static std::vector<T> rb(const char* p){
  std::ifstream f(p,std::ios::binary|std::ios::ate); size_t n=f.tellg(); f.seekg(0);
  std::vector<T> v(n/sizeof(T)); f.read((char*)v.data(),n); return v; }

int main(int argc,char**argv){
  if(argc<10){fprintf(stderr,"usage: %s init out N nticks t0 gin gsg gKw gbias\n",argv[0]);return 2;}
  int N=atoi(argv[3]), nticks=atoi(argv[4]); double t0=atof(argv[5]);
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
  cl_kernel k=clCreateKernel(prog,"run_gate",&err);

  auto init=rb<double>(argv[1]); auto gin=rb<int>(argv[6]); auto gsg=rb<double>(argv[7]);
  auto gKw=rb<double>(argv[8]); auto gbias=rb<double>(argv[9]);
  cl_mem bA=clCreateBuffer(ctx,CL_MEM_READ_WRITE|CL_MEM_COPY_HOST_PTR,N*8*sizeof(double),init.data(),&err);
  cl_mem bB=clCreateBuffer(ctx,CL_MEM_READ_WRITE,N*8*sizeof(double),nullptr,&err);
  cl_mem bGin=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gin.size()*sizeof(int),gin.data(),&err);
  cl_mem bGsg=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gsg.size()*sizeof(double),gsg.data(),&err);
  cl_mem bGKw=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gKw.size()*sizeof(double),gKw.data(),&err);
  cl_mem bGb=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gbias.size()*sizeof(double),gbias.data(),&err);
  cl_mem in=bA,out=bB; std::vector<double> fin(N*8);
  auto T0=std::chrono::high_resolution_clock::now();
  for(int i=0;i<nticks;i++){
    double t=t0+(double)i*DT; double W=0.225+0.018*std::sin(1.8*t);
    clSetKernelArg(k,0,sizeof(cl_mem),&in);clSetKernelArg(k,1,sizeof(cl_mem),&out);
    clSetKernelArg(k,2,sizeof(cl_mem),&bGin);clSetKernelArg(k,3,sizeof(cl_mem),&bGsg);
    clSetKernelArg(k,4,sizeof(cl_mem),&bGKw);clSetKernelArg(k,5,sizeof(cl_mem),&bGb);
    clSetKernelArg(k,6,sizeof(int),&N);clSetKernelArg(k,7,sizeof(double),&W);
    size_t g=((size_t)(N+63)/64)*64; clEnqueueNDRangeKernel(q,k,1,nullptr,&g,nullptr,0,nullptr,nullptr);
    cl_mem tmp=in;in=out;out=tmp;
  }
  clFinish(q);
  auto T1=std::chrono::high_resolution_clock::now();
  fprintf(stderr,"GPU-GATE N=%d nticks=%d: %.2f ms (%.0f ticks/s)\n",N,nticks,
    std::chrono::duration_cast<std::chrono::microseconds>(T1-T0).count()/1000.0,
    nticks/(std::chrono::duration_cast<std::chrono::microseconds>(T1-T0).count()/1e6));
  clEnqueueReadBuffer(q,in,CL_TRUE,0,N*8*sizeof(double),fin.data(),0,nullptr,nullptr);  // in=letzter Out nach swap
  std::ofstream of(argv[2],std::ios::binary); of.write((char*)fin.data(),N*8*sizeof(double));
  return 0;
}
