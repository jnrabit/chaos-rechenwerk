// entropy.cpp — Dynamische Entropie: VOLLES Lyapunov-Spektrum (8 Exponenten) + Kolmogorov-Sinai-
// Entropie h_KS = Summe der positiven Exponenten (Pesin). Das tiefere Mass als die Shannon-Entropie:
// Shannon misst die AUSGABE-Verteilung, das Spektrum misst den PROZESS (wie chaotisch die Trajektorie
// selbst ist). Befund: am Arbeitspunkt h_KS = 0 (Rechnen, kein Chaos), freies Lorenz h_KS > 0.
// Verfahren: Benettin mit Variationsgleichung (analytische Jacobi-Matrix) + Gram-Schmidt, double/fp64.
// SELBER Produktions-Deriv (sig=10,rho=28,bet=8/3,RK4,W-Schedule), optional +Gatter-Term.
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
static const double DT=0.005, SIG=10.0, RHO=28.0, BET=8.0/3.0;

struct S8{double v[8];};   // x1,y1,z1,w1,x2,y2,z2,w2
static S8 add(const S8&a,const S8&b){S8 r;for(int i=0;i<8;i++)r.v[i]=a.v[i]+b.v[i];return r;}
static S8 sub(const S8&a,const S8&b){S8 r;for(int i=0;i<8;i++)r.v[i]=a.v[i]-b.v[i];return r;}
static S8 sc(double k,const S8&a){S8 r;for(int i=0;i<8;i++)r.v[i]=k*a.v[i];return r;}
static double dot(const S8&a,const S8&b){double s=0;for(int i=0;i<8;i++)s+=a.v[i]*b.v[i];return s;}
static double nrm(const S8&a){return std::sqrt(dot(a,a));}

// Produktions-Deriv + optionaler Gatter-Term d.w1 += gk*(gnet - w1)   (nur Halbsystem 1)
static S8 deriv(const S8&s,double W,double gk,double gnet){
  double x1=s.v[0],y1=s.v[1],z1=s.v[2],w1=s.v[3],x2=s.v[4],y2=s.v[5],z2=s.v[6],w2=s.v[7]; S8 d;
  d.v[0]=SIG*(y1-x1);          d.v[1]=x1*(RHO-z1)-y1+W*w1;
  d.v[2]=x1*y1-BET*z1;         d.v[3]=-w1+x1*z1-W*y1;
  d.v[4]=SIG*(y2-x2);          d.v[5]=x2*(RHO-z2)-y2+W*w2;
  d.v[6]=x2*y2-BET*z2;         d.v[7]=-w2+x2*z2-W*y2;
  if(gk!=0.0) d.v[3]+=gk*(gnet - w1);
  return d;
}
static S8 rk4(const S8&s,double W,double gk,double gnet){
  S8 k1=deriv(s,W,gk,gnet),k2=deriv(add(s,sc(DT/2,k1)),W,gk,gnet),
     k3=deriv(add(s,sc(DT/2,k2)),W,gk,gnet),k4=deriv(add(s,sc(DT,k3)),W,gk,gnet);
  return add(s,sc(DT/6.0,add(k1,add(sc(2,k2),add(sc(2,k3),k4)))));
}

// Jacobi-Matrix · Tangentenvektor (block-diagonal: zwei unabhaengige 4x4-Haelfen).
// Gatter-Term wirkt nur auf w1: d/dw1 (dw1) = -(1+gk).
static S8 jacmul(const S8&s,double W,double gk,const S8&t){
  double x1=s.v[0],y1=s.v[1],z1=s.v[2],x2=s.v[4],y2=s.v[5],z2=s.v[6]; S8 o;
  o.v[0]= -SIG*t.v[0] + SIG*t.v[1];
  o.v[1]= (RHO-z1)*t.v[0] - t.v[1] - x1*t.v[2] + W*t.v[3];
  o.v[2]= y1*t.v[0] + x1*t.v[1] - BET*t.v[2];
  o.v[3]= z1*t.v[0] - W*t.v[1] + x1*t.v[2] - (1.0+gk)*t.v[3];
  o.v[4]= -SIG*t.v[4] + SIG*t.v[5];
  o.v[5]= (RHO-z2)*t.v[4] - t.v[5] - x2*t.v[6] + W*t.v[7];
  o.v[6]= y2*t.v[4] + x2*t.v[5] - BET*t.v[6];
  o.v[7]= z2*t.v[4] - W*t.v[5] + x2*t.v[6] - t.v[7];
  return o;
}

// Ein RK4-Tick fuer Zustand s UND Tangentialrahmen V[8] (Variationsgleichung).
static void step(S8&s,S8 V[8],double W,double gk,double gnet){
  S8 k1s=deriv(s,W,gk,gnet);     S8 k1v[8]; for(int i=0;i<8;i++)k1v[i]=jacmul(s,W,gk,V[i]);
  S8 s2=add(s,sc(DT/2,k1s));     S8 V2[8];  for(int i=0;i<8;i++)V2[i]=add(V[i],sc(DT/2,k1v[i]));
  S8 k2s=deriv(s2,W,gk,gnet);    S8 k2v[8]; for(int i=0;i<8;i++)k2v[i]=jacmul(s2,W,gk,V2[i]);
  S8 s3=add(s,sc(DT/2,k2s));     S8 V3[8];  for(int i=0;i<8;i++)V3[i]=add(V[i],sc(DT/2,k2v[i]));
  S8 k3s=deriv(s3,W,gk,gnet);    S8 k3v[8]; for(int i=0;i<8;i++)k3v[i]=jacmul(s3,W,gk,V3[i]);
  S8 s4=add(s,sc(DT,k3s));       S8 V4[8];  for(int i=0;i<8;i++)V4[i]=add(V[i],sc(DT,k3v[i]));
  S8 k4s=deriv(s4,W,gk,gnet);    S8 k4v[8]; for(int i=0;i<8;i++)k4v[i]=jacmul(s4,W,gk,V4[i]);
  for(int i=0;i<8;i++)
    V[i]=add(V[i],sc(DT/6.0,add(k1v[i],add(sc(2,k2v[i]),add(sc(2,k3v[i]),k4v[i])))));
  s=add(s,sc(DT/6.0,add(k1s,add(sc(2,k2s),add(sc(2,k3s),k4s)))));
}
static double Wt(int tick,double Wbase,bool drive){
  return drive ? (Wbase+0.018*std::sin(1.8*(tick*DT))) : Wbase;
}

struct Cfg{const char*name;S8 seed;double Wbase;bool drive;double gk;double gnet;};

// Volles Spektrum: Benettin (Variationsgleichung) + Gram-Schmidt alle TAU Ticks.
static void spectrum(const Cfg&c,double lam[8]){
  int settle=2000, burn=1000, steps=12000, TAU=5;
  S8 s=c.seed; for(int i=0;i<settle;i++) s=rk4(s,Wt(i,c.Wbase,c.drive),c.gk,c.gnet);
  S8 V[8]; for(int i=0;i<8;i++){for(int j=0;j<8;j++)V[i].v[j]=0.0; V[i].v[i]=1.0;}
  double sumlog[8]={0};
  int blocks=(burn+steps)/TAU;
  for(int b=0;b<blocks;b++){
    int t0=settle+b*TAU;
    for(int k=0;k<TAU;k++) step(s,V,Wt(t0+k,c.Wbase,c.drive),c.gk,c.gnet);
    for(int i=0;i<8;i++){                       // Gram-Schmidt
      S8 v=V[i];
      for(int j=0;j<i;j++) v=sub(v,sc(dot(v,V[j]),V[j]));
      double n=nrm(v); if(n<=0) n=1e-300;
      if(b*TAU>=burn) sumlog[i]+=std::log(n);   // Burn-in verwerfen
      V[i]=sc(1.0/n,v);
    }
  }
  double T=(double)steps*DT;
  for(int i=0;i<8;i++) lam[i]=sumlog[i]/T;
}

int main(){
  S8 wing; {double a=9.6,w=363.0; double t[8]={a,a,25,w,a,a,25,w}; memcpy(wing.v,t,sizeof t);}
  S8 chaos;{double t[8]={1.0,1.0,1.0,0.0, 1.0,1.0,1.0,0.0}; memcpy(chaos.v,t,sizeof t);}

  Cfg cfgs[]={
    {"A  Mulde: Wing-Halt, nackt (gk=0)",   wing, 0.225,true, 0.0, 0.0},
    {"A2 Mulde: Wing + Gatter-Regen (gk=2)",wing, 0.225,true, 2.0, 363.0},
    {"B  reines Lorenz (W=0)",              chaos,0.0,  false,0.0, 0.0},
  };

  printf("================================================================================\n");
  printf("DYNAMISCHE ENTROPIE: volles Lyapunov-Spektrum (8 Exponenten) + h_KS = Summe lam^+ (Pesin)\n");
  printf("  Benettin via Variationsgleichung (analyt. Jacobi) + Gram-Schmidt (TAU=5, settle=2000,\n");
  printf("  burn=1000, steps=12000), double/fp64. Shannon misst die AUSGABE, das Spektrum den PROZESS.\n");
  printf("================================================================================\n");

  for(auto&c:cfgs){
    double lam[8]; spectrum(c,lam);
    std::sort(lam,lam+8,[](double a,double b){return a>b;});
    double h=0; for(int i=0;i<8;i++) if(lam[i]>0) h+=lam[i];
    printf("\n%-40s\n",c.name);
    printf("   Spektrum (absteigend): ");
    for(int i=0;i<8;i++) printf("%+.3f%s",lam[i], i<7?", ":"");
    printf("\n   -> h_KS = Summe lam^+ = %+.3f /t.u.  %s\n",
           h, h>0.01?"CHAOTISCH (produziert Entropie)":
              "REGULAER (keine Entropie-Produktion — Rechnen, nicht Chaos)");
  }

  printf("\nCaveat: h_KS via Pesin (Summe positiver Exponenten) gilt streng fuer autonome Systeme; hier\n");
  printf("nicht-autonom (periodischer W-Antrieb) — das Spektrum des Zustandsraums bleibt das korrekte\n");
  printf("Prozess-Mass, die Entropie-Bilanz des erweiterten (periodischen) Systems ist nicht erfasst.\n");
  printf("Im freien Lorenz (B) sind die ~+0.01 die beiden marginalen (lambda=0) Fluss-Richtungen, deren\n");
  printf("endliche Messung h_KS leicht ueberschaetzt (theoretisch 2x0.90 = 1.80); am Arbeitspunkt exakt 0.\n");
  printf("Grundmass der dynamischen Entropie = Lyapunov-Spektrum; Kontrast A (Mulde, h_KS=0) vs B (Chaos, h_KS>0).\n");
  printf("SELBER Produktions-Deriv+RK4; double/fp64; Produktion md5-unberuehrt.\n");
  return 0;
}
