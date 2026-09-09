#!/usr/bin/env python3
# make_figures.py — erzeugt die Abbildungen fuer das Paper (paper/main.tex) aus der PUBLIZIERTEN
# Dynamik (Doppelkern-Lorenz, RK4, dt=0.005, sigma=10/rho=28/beta=8/3, W=0.225+0.018 sin(1.8t)).
# Ausgabe: fig_bistabilitaet.pdf, fig_lyapunov.pdf (Vektor, publikationsfaehig).
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

DT = 0.005; SIG = 10.0; RHO = 28.0; BET = 8.0/3.0

def deriv(s, W, gk=0.0, gnet=0.0):
    x1,y1,z1,w1,x2,y2,z2,w2 = s
    d = np.empty(8)
    d[0]=SIG*(y1-x1); d[1]=x1*(RHO-z1)-y1+W*w1; d[2]=x1*y1-BET*z1; d[3]=-w1+x1*z1-W*y1
    d[4]=SIG*(y2-x2); d[5]=x2*(RHO-z2)-y2+W*w2; d[6]=x2*y2-BET*z2; d[7]=-w2+x2*z2-W*y2
    if gk != 0.0: d[3] += gk*(gnet - w1)
    return d

def rk4(s, W, gk=0.0, gnet=0.0):
    k1=deriv(s,W,gk,gnet); k2=deriv(s+DT/2*k1,W,gk,gnet)
    k3=deriv(s+DT/2*k2,W,gk,gnet); k4=deriv(s+DT*k3,W,gk,gnet)
    return s + DT/6*(k1 + 2*k2 + 2*k3 + k4)

def jacmul(s, W, gk, t):
    x1,y1,z1,w1,x2,y2,z2,w2 = s
    o = np.empty(8)
    o[0]= -SIG*t[0] + SIG*t[1]
    o[1]= (RHO-z1)*t[0] - t[1] - x1*t[2] + W*t[3]
    o[2]= y1*t[0] + x1*t[1] - BET*t[2]
    o[3]= z1*t[0] - W*t[1] + x1*t[2] - (1.0+gk)*t[3]
    o[4]= -SIG*t[4] + SIG*t[5]
    o[5]= (RHO-z2)*t[4] - t[5] - x2*t[6] + W*t[7]
    o[6]= y2*t[4] + x2*t[5] - BET*t[6]
    o[7]= z2*t[4] - W*t[5] + x2*t[6] - t[7]
    return o

def wing(sign): return np.array([9.6,9.6,25.0,sign*363.0, 9.6,9.6,25.0,sign*363.0])

# ---------------------------------------------------------------- Fig. 1: Bistabilitaet
def fig1(path):
    N = 4000
    s = wing(+1)
    ts = np.arange(N)*DT
    w1 = np.empty(N)
    for i in range(N):
        W = 0.225 + 0.018*np.sin(1.8*ts[i])
        s = rk4(s, W)
        w1[i] = s[3]
        if i == 2000: s[3] = -363.0          # Zustands-Stoss: Bit kippen
    fig, ax = plt.subplots(figsize=(6.6, 2.6))
    ax.plot(ts, w1, lw=1.0, color='#1f77b4')
    ax.axvline(ts[2000], color='0.5', ls=':', lw=1.0)
    ax.text(ts[2000], 380, 'Zustands-Stoß', ha='center', fontsize=8)
    ax.set_xlabel('Zeit  $t$')
    ax.set_ylabel('$w_1$')
    ax.set_title('Bistabiler Speicher: die $w$-Komponente hält +/− (zwei Pole), ein Stoß kippt das Bit')
    ax.set_ylim(-420, 420)
    fig.tight_layout(); fig.savefig(path); plt.close(fig)

# ---------------------------------------------------------------- Fig. 2: Lyapunov
def step(s, V, W, gk, gnet):
    k1s=deriv(s,W,gk,gnet); k1v=np.column_stack([jacmul(s,W,gk,V[:,i]) for i in range(8)])
    s2=s+DT/2*k1s; V2=V+DT/2*k1v
    k2s=deriv(s2,W,gk,gnet); k2v=np.column_stack([jacmul(s2,W,gk,V2[:,i]) for i in range(8)])
    s3=s+DT/2*k2s; V3=V+DT/2*k2v
    k3s=deriv(s3,W,gk,gnet); k3v=np.column_stack([jacmul(s3,W,gk,V3[:,i]) for i in range(8)])
    s4=s+DT*k3s; V4=V+DT*k3v
    k4s=deriv(s4,W,gk,gnet); k4v=np.column_stack([jacmul(s4,W,gk,V4[:,i]) for i in range(8)])
    return s+DT/6*(k1s+2*k2s+2*k3s+k4s), V+DT/6*(k1v+2*k2v+2*k3v+k4v)

def spectrum(Wbase, drive, seed, gk=0.0, gnet=0.0, settle=2000, burn=1000, steps=12000, TAU=5):
    s = seed.copy()
    for i in range(settle):
        W = (Wbase + 0.018*np.sin(1.8*(i*DT))) if drive else Wbase
        s = rk4(s, W, gk, gnet)
    V = np.eye(8); sumlog = np.zeros(8); blocks = (burn+steps)//TAU
    for b in range(blocks):
        t0 = settle + b*TAU
        for k in range(TAU):
            W = (Wbase + 0.018*np.sin(1.8*((t0+k)*DT))) if drive else Wbase
            s, V = step(s, V, W, gk, gnet)
        Q = np.zeros_like(V)
        for i in range(8):
            v = V[:,i].copy()
            for j in range(i): v = v - np.dot(v, Q[:,j])*Q[:,j]
            n = np.linalg.norm(v); n = n if n > 0 else 1e-300
            if b*TAU >= burn: sumlog[i] += np.log(n)
            Q[:,i] = v/n
        V = Q
    return np.sort(sumlog/(steps*DT))[::-1]

def logdelta_curve(seed, Wbase, drive, delta0=1e-9, settle=2000, steps=1000):
    a = seed.copy()
    for i in range(settle):
        W = (Wbase + 0.018*np.sin(1.8*(i*DT))) if drive else Wbase
        a = rk4(a, W)
    b = a.copy(); b[0] += delta0
    marks = {10,20,40,60,80,100,150,200,300,400,600,800}
    ts, logd = [], []
    for i in range(steps):
        W = (Wbase + 0.018*np.sin(1.8*((settle+i)*DT))) if drive else Wbase
        a = rk4(a, W); b = rk4(b, W)
        if i+1 in marks:
            ts.append(i+1); logd.append(np.log10(max(np.linalg.norm(a-b), 1e-300)))
    return np.array(ts), np.array(logd)

def fig2(path):
    # (a) Kontraktion vs. Divergenz
    ts_w, ld_w = logdelta_curve(wing(+1), 0.225, True)
    ts_c, ld_c = logdelta_curve(np.array([1,1,1,0,1,1,1,0.]), 0.0, False)
    # (b) volles Spektrum
    lam_wing = spectrum(0.225, True, wing(+1))
    lam_chaos = spectrum(0.0, False, np.array([1,1,1,0,1,1,1,0.]))

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(6.8, 2.6))
    ax1.plot(ts_w, ld_w, 'o-', ms=3, label='Arbeitspunkt (Mulde)', color='#1f77b4')
    ax1.plot(ts_c, ld_c, 's-', ms=3, label='freies Lorenz ($W{=}0$)', color='#d62728')
    ax1.axhline(np.log10(1e-9), color='0.5', ls=':', lw=1.0)
    ax1.set_xlabel('Tick'); ax1.set_ylabel(r'$\log_{10}\,\delta$')
    ax1.legend(fontsize=7); ax1.set_title(r'(a) Störung $\delta_0{=}10^{-9}$')

    x = np.arange(8)
    ax2.bar(x-0.2, lam_wing, 0.4, label='Arbeitspunkt', color='#1f77b4')
    ax2.bar(x+0.2, lam_chaos, 0.4, label='freies Lorenz', color='#d62728')
    ax2.axhline(0, color='k', lw=0.8)
    ax2.set_xticks(x); ax2.set_xlabel('Exponent (absteigend)')
    ax2.set_ylabel(r'$\lambda$'); ax2.legend(fontsize=7)
    ax2.set_title(r'(b) Spektrum, $h_{KS}=\sum\lambda^+$')
    ax2.text(1.0, -0.35, r'$h_{KS}{=}0$', ha='center', fontsize=8, color='#1f77b4')
    ax2.text(6.0, 0.35, r'$h_{KS}{=}'+f'{lam_chaos[lam_chaos>0].sum():.2f}'+r'$', ha='center', fontsize=8, color='#d62728')
    fig.tight_layout(); fig.savefig(path); plt.close(fig)

if __name__ == '__main__':
    import sys, os
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    fig1(os.path.join(out, 'fig_bistabilitaet.pdf'))
    fig2(os.path.join(out, 'fig_lyapunov.pdf'))
    print('Abbildungen geschrieben:', os.path.join(out, 'fig_bistabilitaet.pdf'),
          os.path.join(out, 'fig_lyapunov.pdf'))
