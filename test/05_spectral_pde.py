# 05_spectral_pde.py
import numpy as np from scipy.fft import rfft2, irfft2, rfftfreq, fftfreq import time

                                                                  N = 256 NU = 1e-3 DT = 1e-2 STEPS = 5000 LOG_EVERY = 200

                                                                  x = np.linspace(0, 2 * np.pi, N, endpoint = False) y = np.linspace(0, 2 * np.pi, N, endpoint = False) dx = x[1] - x[0]

                                                                                                                                     kx = fftfreq(N, d = 1.0 / N).astype(np.float64) ky = rfftfreq(N, d = 1.0 / N).astype(np.float64) KX, KY = np.meshgrid(kx, ky, indexing = 'ij') K2 = KX ** 2 + KY ** 2 K2[0, 0] = 1.0

                                                                                                                                                                                                                                                                                                              kmax = N // 3
                                                                                                                                                                                                                                                                                                              dealias =((np.abs(KX) < kmax) & (np.abs(KY) < kmax)).astype(np.float64)

XX, YY = np.meshgrid(x, y, indexing='ij')
omega = (2 * np.cos(XX) * np.cos(YY)
         + 0.5 * np.cos(2*XX)
         + 0.3 * np.sin(3*YY))

def omega_to_uv(omega_hat):
    psi_hat = -omega_hat / K2
    u_hat   = 1j * KY * psi_hat
    v_hat   = -1j * KX * psi_hat
    u = irfft2(u_hat, s=(N, N))
    v = irfft2(v_hat, s=(N, N))
    return u, v

def enstrophy(omega_hat):
    return 0.5 * np.sum(np.abs(omega_hat)**2) / N**4

def energy(omega_hat):
    e_hat = np.abs(omega_hat)**2 / (2.0 * K2)
    e_hat[0, 0] = 0.0
    return np.sum(e_hat) / N**4

IF   = np.exp(-NU * K2 * DT)
IF_h = np.exp(-NU * K2 * DT * 0.5)

def nonlinear(omega_hat):
    o = irfft2(omega_hat * dealias, s=(N, N))
    u, v = omega_to_uv(omega_hat * dealias)
    domega_dx = irfft2(1j * KX * omega_hat * dealias, s=(N, N))
    domega_dy = irfft2(1j * KY * omega_hat * dealias, s=(N, N))
    rhs = -(u * domega_dx + v * domega_dy)
    return rfft2(rhs) * dealias

def rk4_step(omega_hat):
    k1 = nonlinear(omega_hat)
    k2 = nonlinear((omega_hat + 0.5*DT*k1) * IF_h)
    k3 = nonlinear((omega_hat + 0.5*DT*k2) * IF_h)
    k4 = nonlinear((omega_hat * IF) + DT*k3)
    return (omega_hat + DT/6*(k1 + 2*k2 + 2*k3 + k4)) * IF

omega_hat = rfft2(omega)
e0  = energy(omega_hat)
en0 = enstrophy(omega_hat)
print(f"Initial  | E = {e0:.6e} | Z = {en0:.6e}")

t0 = time.time()
for step in range(STEPS):
        omega_hat = rk4_step(omega_hat)

        if step % LOG_EVERY == 0:
                e  = energy(omega_hat)
                en = enstrophy(omega_hat)
                el = time.time() - t0
                print(f"step {step:5d} | E = {e:.6e} | Z = {en:.6e} "
                      f"| dE/E0 = {(e-e0)/e0:+.3e} | {el:.1f}s")
                fflush = lambda: None
                import sys;
sys.stdout.flush() t0 = time.time()

                                omega_final = irfft2(omega_hat, s = (N, N))
        print(f "\nFinal vorticity: min={omega_final.min():.4f} " f
                "max={omega_final.max():.4f} " f
                "mean={omega_final.mean():.6e}")
