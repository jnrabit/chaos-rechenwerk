// lyapunov.cpp — Dynamische Entropie Schritt 1: Lyapunov-Mulde-Test. Misst DIREKT ob die +-363-Mulde
// kontrahiert (lambda<0) oder das Lorenz-Chaos weiterwirkt (lambda>0). Zwei fast-gleiche Trajektorien,
// Benettin-Renormierung (winzig+linear, nie Saettigung) + roher delta(t)-Verlauf. Gemittelt ueber Stoerrichtungen.
// SELBER Produktions-Deriv (sig=10,rho=28,bet=8/3,RK4,W-Schedule), optional +Gatter-Term. double/fp64. md5-unberuehrt.
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
static const double DT=0.005, SIG=10.0, RHO=28.0, BET=8.0/3.0;

struct S8{double v[8];};   // x1,y1,z1,w1,x2,y2,z2,w2
static S8 add(const S8&a,const S8&b){S8 r;for(int i=0;i<8;i++)r.v[i]=a.v[i]+b.v[i];return r;}
static S8 sc(double k,const S8&a){S8 r;for(int i=0;i<8;i++)r.v[i]=k*a.v[i];return r;}
// Produktions-Deriv + optionaler Gatter-Term d.w1 += gk*(gnet - w1) (gnet = Anker-Ziel)
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
static double Wt(int tick,double Wbase,bool drive){ // Default-Schedule oder konstant
  return drive ? (Wbase+0.018*std::sin(1.8*(tick*DT))) : Wbase;
}
static double dist(const S8&a,const S8&b){double s=0;for(int i=0;i<8;i++){double d=a.v[i]-b.v[i];s+=d*d;}return std::sqrt(s);}

struct Cfg{const char*name;S8 seed;double Wbase;bool drive;double gk;double gnet;};

// Benettin-Lyapunov: einschwingen, dann Zwilling mit delta0 auf Komponente comp, alle TAU Ticks renormieren.
static double benettin(const Cfg&c,int comp,double delta0,int settle,int steps,int TAU){
  S8 a=c.seed; for(int i=0;i<settle;i++) a=rk4(a,Wt(i,c.Wbase,c.drive),c.gk,c.gnet);
  S8 b=a; b.v[comp]+=delta0;
  double sumlog=0; double T=0;
  for(int i=0;i<steps;i++){ int tk=settle+i; double W=Wt(tk,c.Wbase,c.drive);
    a=rk4(a,W,c.gk,c.gnet); b=rk4(b,W,c.gk,c.gnet);
    if((i+1)%TAU==0){ double d=dist(a,b); if(d<=0) d=1e-300;
      sumlog+=std::log(d/delta0); T+=TAU*DT;
      double sf=delta0/d; for(int k=0;k<8;k++) b.v[k]=a.v[k]+(b.v[k]-a.v[k])*sf; }
  }
  return (T>0)? sumlog/T : 0.0;
}
// Roher delta(t) (ohne Renormierung) fuer log-delta-vs-t; gibt log10(delta) an gewaehlten Ticks
static void roh_delta(const Cfg&c,int comp,double delta0,int settle,int steps,std::vector<int>&ts,std::vector<double>&logd){
  S8 a=c.seed; for(int i=0;i<settle;i++) a=rk4(a,Wt(i,c.Wbase,c.drive),c.gk,c.gnet);
  S8 b=a; b.v[comp]+=delta0;
  for(int i=0;i<steps;i++){ int tk=settle+i; double W=Wt(tk,c.Wbase,c.drive);
    a=rk4(a,W,c.gk,c.gnet); b=rk4(b,W,c.gk,c.gnet);
    for(size_t j=0;j<ts.size();j++) if(i+1==ts[j]) logd[j]=std::log10(std::max(dist(a,b),1e-300));
  }
}

int main(){
  double delta0=1e-9; int settle=2000, steps=4000, TAU=20;
  S8 wing; {double a=9.6,w=363.0; double t[8]={a,a,25,w,a,a,25,w}; memcpy(wing.v,t,sizeof t);}
  S8 chaos;{double t[8]={1.0,1.0,1.0,0.0, 1.0,1.0,1.0,0.0}; memcpy(chaos.v,t,sizeof t);}  // Nicht-Wing-Start

  std::vector<Cfg> cfgs={
    {"A1 Mulde: Wing-Halt, nackt (gk=0)",        wing, 0.225,true, 0.0,  0.0},
    {"A2 Mulde: Wing + Gatter-Regen (gk=2)",     wing, 0.225,true, 2.0, 363.0},
    {"B-arb  Arbeitspunkt W=0.225, Nicht-Wing",  chaos,0.225,true, 0.0,  0.0},
    {"B-chaos reines Lorenz (W=0)",              chaos,0.0,  false,0.0,  0.0},
    {"B-drive W=0.26 (Bifurkation +15%)",        chaos,0.26, true, 0.0,  0.0},
  };
  int comps[4]={0,1,2,3}; const char* cn[4]={"x1","y1","z1","w1"};

  printf("================================================================================\n");
  printf("LYAPUNOV-MULDE-TEST: kontrahiert die +-363-Mulde (l<0) oder wirkt das Chaos (l>0)?\n");
  printf("  Benettin-Renormierung (delta0=%.0e, TAU=%d, settle=%d, steps=%d), gemittelt ueber x1/y1/z1/w1\n",delta0,TAU,settle,steps);
  printf("  reine Dynamik, KEINE Hardware-Kopplung, deterministisch; SELBER Produktions-Deriv (+opt Gatter-Term)\n");
  printf("================================================================================\n");

  for(auto&c:cfgs){
    double lam[4], lmax=-1e9;
    for(int j=0;j<4;j++){ lam[j]=benettin(c,comps[j],delta0,settle,steps,TAU); if(lam[j]>lmax)lmax=lam[j]; }
    printf("\n%-42s\n",c.name);
    printf("   lambda je Stoerrichtung: x1=%+.4f y1=%+.4f z1=%+.4f w1=%+.4f /t.u.\n",lam[0],lam[1],lam[2],lam[3]);
    // Massgeblich = lambda_max (dominante Rate); Divergenz gdw lambda_max>0
    double tau_d = (lmax!=0)? 1.0/std::fabs(lmax) : 0;
    printf("   -> lambda_MAX = %+.4f /t.u.  %s  (Zeitskala 1/|l_max| = %.3f t.u. = %.0f Ticks)\n",
           lmax, lmax<-0.01?"KONTRAHIEREND (Mulde baendigt — ALLE Richtungen <0)":lmax>0.01?"DIVERGIEREND (Chaos wirkt)":"NEUTRAL/marginal",
           tau_d, tau_d/DT);
  }

  // log-delta-vs-t fuer den linearen Mess-Bereich (eine Richtung w1, je eine Mulde- und Chaos-Config)
  printf("\n--- log10(delta) vs Tick (roh, ohne Renormierung) — linearer Bereich sichtbar ---\n");
  std::vector<int> ts={10,20,40,60,80,100,150,200,300,400,600,800};
  printf("   (Stoerung auf x1 = generische Richtung, regt die dominante/maximale Rate an)\n");
  printf("   %-30s |","Config");
  for(int t:ts) printf(" t%-4d",t); printf("\n");
  for(int ci : {0,1,3}){ std::vector<double> logd(ts.size(),NAN);
    roh_delta(cfgs[ci],0,delta0,settle,1000,ts,logd);   // comp=0 (x1), generisch
    printf("   %-30s |",cfgs[ci].name);
    for(double v:logd) printf(" %+.1f",v); printf("\n");
  }
  printf("   (delta0=1e-9 -> log10=-9.0 ; Anstieg=Divergenz, Abfall=Kontraktion; Plateau~log10(Attraktor-Durchm.)=Saettigung)\n");

  printf("\nCaveat: Lyapunov-Grundmass der dynamischen Entropie (misst den PROZESS, nicht die Ausgabe wie Shannon);\n");
  printf("reine Dynamik ohne Hardware-Kopplung; Benettin delta0 winzig+renormiert=lineare Antwort, nie Saettigung; Kontrast A(Mulde)\n");
  printf("vs B(Chaos); SELBER Produktions-Deriv+RK4; double/fp64; Produktion md5-unberuehrt.\n");
  return 0;
}
