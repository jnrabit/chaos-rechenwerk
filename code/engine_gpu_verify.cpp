// engine_gpu_verify.cpp — Doppelkern-Lorenz EINER Zelle als OpenCL-Kernel (double/fp64).
// Exakte Uebersetzung der Produktions-lorenz_deriv + rk4_step. Kein mean-field, keine Hardware.
// Aufruf: ./gpu_verify <init.bin> <out.bin> <M> <nticks> <t0>
//   init.bin = M*8 doubles (Startzustaende), out.bin = M*nticks*8 doubles (Trajektorien)
#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>

const char* kernelSource = R"CLC(
#pragma OPENCL EXTENSION cl_khr_fp64 : enable
typedef struct { double x1,y1,z1,w1,x2,y2,z2,w2; } S8;
static S8 d_add(S8 a, S8 b){ S8 r;
  r.x1=a.x1+b.x1; r.y1=a.y1+b.y1; r.z1=a.z1+b.z1; r.w1=a.w1+b.w1;
  r.x2=a.x2+b.x2; r.y2=a.y2+b.y2; r.z2=a.z2+b.z2; r.w2=a.w2+b.w2; return r; }
static S8 d_sc(double k, S8 a){ S8 r;
  r.x1=k*a.x1; r.y1=k*a.y1; r.z1=k*a.z1; r.w1=k*a.w1;
  r.x2=k*a.x2; r.y2=k*a.y2; r.z2=k*a.z2; r.w2=k*a.w2; return r; }
static S8 deriv(S8 s, double W, double sigma, double rho, double beta){
  S8 d;
  d.x1 = sigma*(s.y1-s.x1);
  d.y1 = s.x1*(rho-s.z1) - s.y1 + W*s.w1;
  d.z1 = s.x1*s.y1 - beta*s.z1;
  d.w1 = -s.w1 + s.x1*s.z1 - W*s.y1;
  d.x2 = sigma*(s.y2-s.x2);
  d.y2 = s.x2*(rho-s.z2) - s.y2 + W*s.w2;
  d.z2 = s.x2*s.y2 - beta*s.z2;
  d.w2 = -s.w2 + s.x2*s.z2 - W*s.y2;
  return d;
}
static S8 rk4(S8 s, double W, double sigma, double rho, double beta, double DT){
  S8 k1 = deriv(s, W, sigma,rho,beta);
  S8 k2 = deriv(d_add(s, d_sc(DT/2.0, k1)), W, sigma,rho,beta);
  S8 k3 = deriv(d_add(s, d_sc(DT/2.0, k2)), W, sigma,rho,beta);
  S8 k4 = deriv(d_add(s, d_sc(DT,     k3)), W, sigma,rho,beta);
  // EXAKT die Produktions-Klammerung: s + DT/6*(k1 + (2k2 + (2k3 + k4)))
  return d_add(s, d_sc(DT/6.0, d_add(k1, d_add(d_sc(2.0,k2), d_add(d_sc(2.0,k3), k4)))));
}
__kernel void run(__global const double* init, __global double* out,
                  int M, int nticks, double t0){
  int gid = get_global_id(0);
  if(gid >= M) return;
  const double SIG=10.0, RHO=28.0, BET=8.0/3.0, DT=0.005;
  S8 s;
  s.x1=init[gid*8+0]; s.y1=init[gid*8+1]; s.z1=init[gid*8+2]; s.w1=init[gid*8+3];
  s.x2=init[gid*8+4]; s.y2=init[gid*8+5]; s.z2=init[gid*8+6]; s.w2=init[gid*8+7];
  for(int i=0;i<nticks;i++){
    double t = t0 + (double)i * DT;
    double W = 0.225 + 0.018*sin(1.8*t);
    s = rk4(s, W, SIG, RHO, BET, DT);
    int o = (gid*nticks + i)*8;
    out[o+0]=s.x1; out[o+1]=s.y1; out[o+2]=s.z1; out[o+3]=s.w1;
    out[o+4]=s.x2; out[o+5]=s.y2; out[o+6]=s.z2; out[o+7]=s.w2;
  }
}
)CLC";

static std::vector<double> read_bin(const char* p){
  std::ifstream f(p, std::ios::binary|std::ios::ate); size_t n=f.tellg(); f.seekg(0);
  std::vector<double> v(n/sizeof(double)); f.read((char*)v.data(), n); return v;
}

int main(int argc, char** argv){
  if(argc<6){ fprintf(stderr,"usage: %s init.bin out.bin M nticks t0\n",argv[0]); return 2; }
  const char* initf=argv[1]; const char* outf=argv[2];
  int M=atoi(argv[3]); int nticks=atoi(argv[4]); double t0=atof(argv[5]);

  cl_int err; cl_uint nplat=0;
  clGetPlatformIDs(0,nullptr,&nplat);
  std::vector<cl_platform_id> plats(nplat); clGetPlatformIDs(nplat,plats.data(),nullptr);
  cl_device_id dev=nullptr; cl_platform_id usedplat=nullptr;
  for(auto p: plats){ // AMD APP-Plattform mit GPU bevorzugen
    char pn[128]={0}; clGetPlatformInfo(p,CL_PLATFORM_NAME,sizeof(pn),pn,nullptr);
    cl_device_id d=nullptr;
    if(clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,1,&d,nullptr)==CL_SUCCESS && d){
      if(strstr(pn,"AMD Accelerated")){ dev=d; usedplat=p; break; }
      if(!dev){ dev=d; usedplat=p; }
    }
  }
  if(!dev){ for(auto p: plats){ cl_device_id d=nullptr; if(clGetDeviceIDs(p,CL_DEVICE_TYPE_CPU,1,&d,nullptr)==CL_SUCCESS&&d){dev=d;usedplat=p;break;} } }
  if(!dev){ fprintf(stderr,"no OpenCL device\n"); return 1; }
  char dn[256]={0}, ext[4096]={0};
  clGetDeviceInfo(dev,CL_DEVICE_NAME,sizeof(dn),dn,nullptr);
  clGetDeviceInfo(dev,CL_DEVICE_EXTENSIONS,sizeof(ext),ext,nullptr);
  fprintf(stderr,"Device: %s | fp64: %s\n", dn, strstr(ext,"cl_khr_fp64")?"JA":"NEIN");

  cl_context ctx=clCreateContext(nullptr,1,&dev,nullptr,nullptr,&err);
  cl_command_queue q=clCreateCommandQueue(ctx,dev,0,&err);
  cl_program prog=clCreateProgramWithSource(ctx,1,&kernelSource,nullptr,&err);
  err=clBuildProgram(prog,1,&dev,nullptr,nullptr,nullptr);
  if(err!=CL_SUCCESS){ char log[8192]={0};
    clGetProgramBuildInfo(prog,dev,CL_PROGRAM_BUILD_LOG,sizeof(log),log,nullptr);
    fprintf(stderr,"BUILD ERROR:\n%s\n",log); return 1; }
  cl_kernel k=clCreateKernel(prog,"run",&err);

  std::vector<double> init=read_bin(initf);
  size_t out_n=(size_t)M*nticks*8;
  cl_mem b_init=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR, init.size()*sizeof(double), init.data(), &err);
  cl_mem b_out =clCreateBuffer(ctx,CL_MEM_WRITE_ONLY, out_n*sizeof(double), nullptr, &err);
  clSetKernelArg(k,0,sizeof(cl_mem),&b_init);
  clSetKernelArg(k,1,sizeof(cl_mem),&b_out);
  clSetKernelArg(k,2,sizeof(int),&M);
  clSetKernelArg(k,3,sizeof(int),&nticks);
  clSetKernelArg(k,4,sizeof(double),&t0);
  size_t global=((M+63)/64)*64;
  err=clEnqueueNDRangeKernel(q,k,1,nullptr,&global,nullptr,0,nullptr,nullptr);
  clFinish(q);
  std::vector<double> out(out_n);
  clEnqueueReadBuffer(q,b_out,CL_TRUE,0,out_n*sizeof(double),out.data(),0,nullptr,nullptr);
  std::ofstream of(outf,std::ios::binary); of.write((char*)out.data(), out_n*sizeof(double));
  clReleaseMemObject(b_init); clReleaseMemObject(b_out); clReleaseKernel(k);
  clReleaseProgram(prog); clReleaseCommandQueue(q); clReleaseContext(ctx);
  return 0;
}
