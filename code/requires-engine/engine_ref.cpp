// engine_ref.cpp — Clean-Room-Referenz der VERÖFFENTLICHTEN Dynamik.
//
// Zweck: die requires-engine-Verifikationsskripte (verify_gpu.py, verify_gpu_mf.py,
// verify_gpu_red.py, verify_gate.py, verify_chain.py) lauffähig machen, OHNE die
// nicht-veröffentlichte Produktions-Engine. Diese Referenz implementiert exakt die im
// BEWEIS / LEITFADEN publizierte Dynamik:
//   - Doppelkern-Lorenz (S8 = x1,y1,z1,w1,x2,y2,z2,w2), RK4, DT=0.005
//   - σ=10, ρ=28, β=8/3, Antrieb W = 0.225 + 0.018·sin(1.8·t)
//   - Mean-Field nur auf x1/x2: dx += K·(mean − x), K=0.05, Schnappschuss am Tick-Anfang
//   - Gatter-Term: dw1 += gk·(sign(NET)·363 − w1), NET = Σ gsg·w1(Eingang) + bias
//
// WICHTIG (ehrlich): Dies ist NICHT die Produktions-Engine (md5 3a6344c…). Es ist eine
// unabhängige Neuimplementierung desselben publizierten Derivs. Der Vergleich GPU ↔ Referenz
// belegt daher die GPU-Kernel gegen eine unabhängige C++-Implementierung — bit-nah für die
// Dynamik (~1e-13 über 1000 Ticks), bit-nah für die Gatter-Logik (~1e-14; die *logische*
// Ausgabe, das Vorzeichen, ist exakt korrekt) — nicht gegen das private Produktions-Binary.
// Die Methodik ist identisch; die Referenz ist neu. Die im LEITFADEN notierte bit-*exakte*
// (0.0) Übereinstimmung gilt nur gegenüber der Produktions-Engine selbst.
//
// Baut als libengine_ref.so, libengine_ref_mf.so und libengine_ref_gate.so (alle drei
// Symbole in jeder .so; die Skripte laden je nach Bedarf eine der drei).
#include <cmath>
#include <vector>
#include <cstddef>

static const double DT=0.005, SIG=10.0, RHO=28.0, BET=8.0/3.0, KMF=0.05;

struct S8 { double x1,y1,z1,w1,x2,y2,z2,w2; };

static inline S8 add(const S8& a, const S8& b){ S8 r;
  r.x1=a.x1+b.x1; r.y1=a.y1+b.y1; r.z1=a.z1+b.z1; r.w1=a.w1+b.w1;
  r.x2=a.x2+b.x2; r.y2=a.y2+b.y2; r.z2=a.z2+b.z2; r.w2=a.w2+b.w2; return r; }
static inline S8 sc(double k, const S8& a){ S8 r;
  r.x1=k*a.x1; r.y1=k*a.y1; r.z1=k*a.z1; r.w1=k*a.w1;
  r.x2=k*a.x2; r.y2=k*a.y2; r.z2=k*a.z2; r.w2=k*a.w2; return r; }

static inline double Wt(double t){ return 0.225 + 0.018*std::sin(1.8*t); }

// Basis-Deriv (eine Zelle, kein Gatter, kein mean-field) — identisch zum OpenCL-Kernel.
static inline S8 deriv(const S8& s, double W){ S8 d;
  d.x1 = SIG*(s.y1-s.x1);
  d.y1 = s.x1*(RHO-s.z1) - s.y1 + W*s.w1;
  d.z1 = s.x1*s.y1 - BET*s.z1;
  d.w1 = -s.w1 + s.x1*s.z1 - W*s.y1;
  d.x2 = SIG*(s.y2-s.x2);
  d.y2 = s.x2*(RHO-s.z2) - s.y2 + W*s.w2;
  d.z2 = s.x2*s.y2 - BET*s.z2;
  d.w2 = -s.w2 + s.x2*s.z2 - W*s.y2;
  return d; }

static inline S8 deriv_mf(const S8& s, double W, double mx1, double mx2){ S8 d=deriv(s,W);
  d.x1 += KMF*(mx1 - s.x1); d.x2 += KMF*(mx2 - s.x2); return d; }

static inline S8 deriv_gate(const S8& s, double W, double gk, double gnet){ S8 d=deriv(s,W);
  if(gk!=0.0) d.w1 += gk*(gnet - s.w1); return d; }

// RK4 mit derselben Klammerung wie der Kernel: s + DT/6·(k1 + (2k2 + (2k3 + k4)))
template<class F> static inline S8 rk4(const S8& s, F f){
  S8 k1=f(s);
  S8 k2=f(add(s, sc(DT/2.0, k1)));
  S8 k3=f(add(s, sc(DT/2.0, k2)));
  S8 k4=f(add(s, sc(DT,     k3)));
  return add(s, sc(DT/6.0, add(k1, add(sc(2.0,k2), add(sc(2.0,k3), k4)))));
}

extern "C" {

// Einzelzelle (kein mean-field, kein Gatter). out[i*8+c] = Zustand nach i+1 Schritten.
void ref_steps(const double* s8, int nticks, double t0, double* out){
  S8 s; s.x1=s8[0];s.y1=s8[1];s.z1=s8[2];s.w1=s8[3];
        s.x2=s8[4];s.y2=s8[5];s.z2=s8[6];s.w2=s8[7];
  for(int i=0;i<nticks;i++){
    double W=Wt(t0+(double)i*DT);
    s=rk4(s,[&](const S8& q){return deriv(q,W);});
    out[i*8+0]=s.x1;out[i*8+1]=s.y1;out[i*8+2]=s.z1;out[i*8+3]=s.w1;
    out[i*8+4]=s.x2;out[i*8+5]=s.y2;out[i*8+6]=s.z2;out[i*8+7]=s.w2;
  }
}

// N Zellen + mean-field (Schnappschuss am Tick-Anfang). out[(n*nticks+i)*8+c]. out==NULL → nur rechnen.
void ref_steps_mf(double* st, int N, int nticks, double t0, double* out){
  for(int i=0;i<nticks;i++){
    double W=Wt(t0+(double)i*DT);
    double mx1=0,mx2=0;
    for(int n=0;n<N;n++){ mx1+=st[n*8+0]; mx2+=st[n*8+4]; }
    mx1/=(double)N; mx2/=(double)N;
    for(int n=0;n<N;n++){
      S8 s; s.x1=st[n*8+0];s.y1=st[n*8+1];s.z1=st[n*8+2];s.w1=st[n*8+3];
            s.x2=st[n*8+4];s.y2=st[n*8+5];s.z2=st[n*8+6];s.w2=st[n*8+7];
      s=rk4(s,[&](const S8& q){return deriv_mf(q,W,mx1,mx2);});
      st[n*8+0]=s.x1;st[n*8+1]=s.y1;st[n*8+2]=s.z1;st[n*8+3]=s.w1;
      st[n*8+4]=s.x2;st[n*8+5]=s.y2;st[n*8+6]=s.z2;st[n*8+7]=s.w2;
      if(out) for(int c=0;c<8;c++) out[((size_t)n*nticks+i)*8+c]=st[n*8+c];
    }
  }
}

// N Zellen + Gatter-Primitiv (sign(NET)·363-Regeneration). out[(n*nticks+i)*8+c].
void ref_gate_steps(double* st, int N, const int* gin, const double* gsg,
                    const double* gKw, const double* gbias, int nticks, double t0, double* out){
  std::vector<double> gnet(N);
  for(int i=0;i<nticks;i++){
    double W=Wt(t0+(double)i*DT);
    for(int g=0;g<N;g++){                                   // NET aus Tick-Anfangs-Schnappschuss
      double k=gKw[g];
      if(k!=0.0){
        double net=gbias[g];
        for(int j=0;j<4;j++){ int idx=gin[g*4+j]; if(idx>=0) net += gsg[g*4+j]*st[idx*8+3]; }
        gnet[g]=(net>=0.0?363.0:-363.0);
      }
    }
    for(int g=0;g<N;g++){
      double k=gKw[g];
      S8 s; s.x1=st[g*8+0];s.y1=st[g*8+1];s.z1=st[g*8+2];s.w1=st[g*8+3];
            s.x2=st[g*8+4];s.y2=st[g*8+5];s.z2=st[g*8+6];s.w2=st[g*8+7];
      s=rk4(s,[&](const S8& q){return deriv_gate(q,W,k,gnet[g]);});
      st[g*8+0]=s.x1;st[g*8+1]=s.y1;st[g*8+2]=s.z1;st[g*8+3]=s.w1;
      st[g*8+4]=s.x2;st[g*8+5]=s.y2;st[g*8+6]=s.z2;st[g*8+7]=s.w2;
      if(out) for(int c=0;c<8;c++) out[((size_t)g*nticks+i)*8+c]=st[g*8+c];
    }
  }
}

} // extern "C"
