// profile_ossifikat.cpp — Host-Steuerungs-Profil von Mini-Ossifikat auf GPU.
// 1 neues Tripel gegen N bestehende auf KONFLIKT pruefen. SEQUENZIELL (N Settles+Syncs) vs PARALLEL (1).
// Persistenter GPU-Kontext (kein Subprocess). Timer: settle(GPU)/read(sync)/write(upload)/cpu(host-logik).
#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cmath>
#include <chrono>
using clk=std::chrono::high_resolution_clock;
static double us(clk::time_point a,clk::time_point b){return std::chrono::duration_cast<std::chrono::microseconds>(b-a).count();}

const char* SRC=R"CLC(
#pragma OPENCL EXTENSION cl_khr_fp64 : enable
typedef struct { double x1,y1,z1,w1,x2,y2,z2,w2; } S8;
static S8 ad(S8 a,S8 b){S8 r;r.x1=a.x1+b.x1;r.y1=a.y1+b.y1;r.z1=a.z1+b.z1;r.w1=a.w1+b.w1;r.x2=a.x2+b.x2;r.y2=a.y2+b.y2;r.z2=a.z2+b.z2;r.w2=a.w2+b.w2;return r;}
static S8 sc(double k,S8 a){S8 r;r.x1=k*a.x1;r.y1=k*a.y1;r.z1=k*a.z1;r.w1=k*a.w1;r.x2=k*a.x2;r.y2=k*a.y2;r.z2=k*a.z2;r.w2=k*a.w2;return r;}
static S8 dv(S8 s,double W,double gk,double gn){
 S8 d; d.x1=10.0*(s.y1-s.x1); d.y1=s.x1*(28.0-s.z1)-s.y1+W*s.w1; d.z1=s.x1*s.y1-(8.0/3.0)*s.z1; d.w1=-s.w1+s.x1*s.z1-W*s.y1;
 d.x2=10.0*(s.y2-s.x2); d.y2=s.x2*(28.0-s.z2)-s.y2+W*s.w2; d.z2=s.x2*s.y2-(8.0/3.0)*s.z2; d.w2=-s.w2+s.x2*s.z2-W*s.y2;
 if(gk!=0.0) d.w1+=gk*(gn-s.w1); return d;}
static S8 rk(S8 s,double W,double gk,double gn){double DT=0.005;
 S8 k1=dv(s,W,gk,gn),k2=dv(ad(s,sc(DT/2,k1)),W,gk,gn),k3=dv(ad(s,sc(DT/2,k2)),W,gk,gn),k4=dv(ad(s,sc(DT,k3)),W,gk,gn);
 return ad(s,sc(DT/6.0,ad(k1,ad(sc(2,k2),ad(sc(2,k3),k4)))));}
__kernel void run_gate(__global const double* in,__global double* out,__global const int* gin,
  __global const double* gsg,__global const double* gKw,__global const double* gbias,int N,double W){
 int g=get_global_id(0); if(g>=N) return; double gk=gKw[g],gn=0.0;
 if(gk!=0.0){ double net=gbias[g]; for(int j=0;j<4;j++){int idx=gin[g*4+j]; if(idx>=0) net+=gsg[g*4+j]*in[idx*8+3];} gn=(net>=0.0?363.0:-363.0);}
 S8 s; s.x1=in[g*8+0];s.y1=in[g*8+1];s.z1=in[g*8+2];s.w1=in[g*8+3];s.x2=in[g*8+4];s.y2=in[g*8+5];s.z2=in[g*8+6];s.w2=in[g*8+7];
 s=rk(s,W,gk,gn);
 out[g*8]=s.x1;out[g*8+1]=s.y1;out[g*8+2]=s.z1;out[g*8+3]=s.w1;out[g*8+4]=s.x2;out[g*8+5]=s.y2;out[g*8+6]=s.z2;out[g*8+7]=s.w2;}
)CLC";

cl_context ctx; cl_command_queue Q; cl_kernel K;
static void wing(double*c,int s){double a=s*9.6,w=s*363.0; c[0]=a;c[1]=a;c[2]=25;c[3]=w;c[4]=a;c[5]=a;c[6]=25;c[7]=w;}
// Konflikt-Netz (16 Zellen) ab Offset o in die Konfig schreiben
static void conf_cfg(int o,int*gin,double*gsg,double*gKw,double*gb){
 auto G=[&](int c,int i0,int i1,int i2,double s0,double s1,double s2,double b){
   gin[c*4]=i0;gin[c*4+1]=i1;gin[c*4+2]=i2;gin[c*4+3]=-1; gsg[c*4]=s0;gsg[c*4+1]=s1;gsg[c*4+2]=s2;gsg[c*4+3]=0; gKw[c]=2.0;gb[c]=b;};
 G(o+6,o+0,o+1,-1,+1,+1,0,+400); G(o+7,o+0,o+1,-1,-1,-1,0,+400); G(o+8,o+6,o+7,-1,-1,-1,0,+400);   // XNOR_S
 G(o+9,o+2,o+3,-1,+1,+1,0,+400); G(o+10,o+2,o+3,-1,-1,-1,0,+400); G(o+11,o+9,o+10,-1,-1,-1,0,+400); // XNOR_P
 G(o+12,o+4,o+5,-1,+1,+1,0,+400); G(o+13,o+4,o+5,-1,-1,-1,0,+400); G(o+14,o+12,o+13,-1,+1,+1,0,-400); // XOR_O
 G(o+15,o+8,o+11,o+14,+1,+1,+1,-800);}  // 3-Eingang-AND

int main(){
 cl_int e; cl_uint np; clGetPlatformIDs(0,0,&np); std::vector<cl_platform_id> ps(np); clGetPlatformIDs(np,ps.data(),0);
 cl_device_id dev=0; for(auto p:ps){char n[128]={0};clGetPlatformInfo(p,CL_PLATFORM_NAME,128,n,0);cl_device_id d=0;
   if(clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,1,&d,0)==CL_SUCCESS&&d){if(strstr(n,"AMD Accelerated")){dev=d;break;}if(!dev)dev=d;}}
 ctx=clCreateContext(0,1,&dev,0,0,&e); Q=clCreateCommandQueue(ctx,dev,0,&e);
 cl_program pr=clCreateProgramWithSource(ctx,1,&SRC,0,&e); clBuildProgram(pr,1,&dev,0,0,0); K=clCreateKernel(pr,"run_gate",&e);
 int M=2000; double DT=0.005;
 printf("Mini-Ossifikat Host-Steuerungs-Profil: 1 Tripel vs N bestehende, Konflikt-Netz, M=%d Settle-Ticks\n",M);
 printf("%-5s %-4s | %6s %6s | %9s %8s %8s %7s | %9s | Engstelle\n","mode","N","nset","nrd","t_settle","t_read","t_write","t_cpu","total(ms)");
 printf("------------------------------------------------------------------------------------------------\n");
 auto run=[&](bool par,int N){
   int NC = par? 16*N : 16;                 // par: alle N Netze; seq: 1 Netz wiederverwendet
   std::vector<double> init(NC*8,0.0); std::vector<int> gin(NC*4,-1); std::vector<double> gsg(NC*4,0),gKw(NC,0),gb(NC,0);
   auto t_cpu0=clk::now();
   if(par){ for(int m=0;m<N;m++){ int o=16*m; int v[6]={1,1,(m%2),1,1,0}; for(int k=0;k<6;k++) wing(&init[(o+k)*8],v[k]?1:-1); for(int k=6;k<16;k++) wing(&init[(o+k)*8],1); conf_cfg(o,gin.data(),gsg.data(),gKw.data(),gb.data()); } }
   else { conf_cfg(0,gin.data(),gsg.data(),gKw.data(),gb.data()); }
   double t_cpu=us(t_cpu0,clk::now());
   cl_mem bA=clCreateBuffer(ctx,CL_MEM_READ_WRITE,NC*8*sizeof(double),0,&e), bB=clCreateBuffer(ctx,CL_MEM_READ_WRITE,NC*8*sizeof(double),0,&e);
   cl_mem bGi=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gin.size()*4,gin.data(),&e);
   cl_mem bGs=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gsg.size()*8,gsg.data(),&e);
   cl_mem bGk=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gKw.size()*8,gKw.data(),&e);
   cl_mem bGb=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,gb.size()*8,gb.data(),&e);
   double t_settle=0,t_read=0,t_write=0; int nset=0,nrd=0; std::vector<double> rd(par?N*8:8);
   int loops = par? 1 : N;
   for(int it=0; it<loops; it++){
     auto tw0=clk::now();
     if(!par){ int v[6]={1,1,(it%2),1,1,0}; for(int k=0;k<6;k++) wing(&init[k*8],v[k]?1:-1); for(int k=6;k<16;k++) wing(&init[k*8],1); }
     clEnqueueWriteBuffer(Q,bA,CL_TRUE,0,NC*8*sizeof(double),init.data(),0,0,0); t_write+=us(tw0,clk::now());
     cl_mem in=bA,out=bB; auto ts0=clk::now();
     for(int i=0;i<M;i++){ double W=0.225+0.018*std::sin(1.8*(i*DT));
       clSetKernelArg(K,0,sizeof(cl_mem),&in);clSetKernelArg(K,1,sizeof(cl_mem),&out);clSetKernelArg(K,2,sizeof(cl_mem),&bGi);
       clSetKernelArg(K,3,sizeof(cl_mem),&bGs);clSetKernelArg(K,4,sizeof(cl_mem),&bGk);clSetKernelArg(K,5,sizeof(cl_mem),&bGb);
       clSetKernelArg(K,6,sizeof(int),&NC);clSetKernelArg(K,7,sizeof(double),&W);
       size_t g=((size_t)(NC+63)/64)*64; clEnqueueNDRangeKernel(Q,K,1,0,&g,0,0,0,0); cl_mem t=in;in=out;out=t; nset++; }
     clFinish(Q); t_settle+=us(ts0,clk::now());
     auto tr0=clk::now();
     if(par){ for(int m=0;m<N;m++){ double w; clEnqueueReadBuffer(Q,in,CL_TRUE,((16*m+15)*8+3)*sizeof(double),sizeof(double),&w,0,0,0); } nrd+=1; }
     else { double w; clEnqueueReadBuffer(Q,in,CL_TRUE,(15*8+3)*sizeof(double),sizeof(double),&w,0,0,0); nrd++; }
     t_read+=us(tr0,clk::now());
   }
   double tot=t_settle+t_read+t_write+t_cpu;
   const char* bn = (t_settle>0.6*tot)?"GPU-bound(Settle)":(t_read>0.3*tot)?"SYNC-bound":(t_write>0.3*tot)?"WRITE-bound":(t_cpu>0.3*tot)?"host-logic":"gemischt";
   printf("%-5s %-4d | %6d %6d | %8.0f %8.0f %8.0f %7.0f | %9.1f | %s\n", par?"PAR":"SEQ", N, nset,nrd, t_settle,t_read,t_write,t_cpu, tot/1000.0, bn);
   clReleaseMemObject(bA);clReleaseMemObject(bB);clReleaseMemObject(bGi);clReleaseMemObject(bGs);clReleaseMemObject(bGk);clReleaseMemObject(bGb);
 };
 for(int N: {8,16,100}) run(false,N);
 printf("- - -\n");
 for(int N: {8,16,100,1000}) run(true,N);
 return 0;
}
