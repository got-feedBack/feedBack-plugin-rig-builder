"""Real Cab — sintetizador paramétrico de IRs de cabinet.

Port consolidado del prototipo validado en `Cabs/prototype/` (ver
`Cabs/CAB_MODELING_GUIDE.md` y su README para la física y las validaciones):

    H(f) = Driver(f) · PosiciónCono(f,x,d) · Directividad(f,θ,d)
           · Mic(f) · Proximity(f,d) · Caja(f)

- Driver: curva SPL digitalizada del datasheet oficial del parlante.
- Posición: 3 zonas del cono (dustcap/medio/borde) con caminos geométricos y
  suma PARCIALMENTE INCOHERENTE (validado: los movimientos anchos son un tilt
  suave sin notches profundos — el cono es fuente distribuida).
- Campo cercano en todo: proximity con distancia efectiva √(d²+r²) y
  directividad de pistón escalada por d/(d+a).
- Caja por CONFIGURACIÓN: closed-back → LF bloom (+11 dB <550 Hz en 4x12,
  medido); open-back → dipolo (−8 dB <140 Hz en el AC30 2x12, medido);
  baffle step por ancho; acople mutuo por número de conos.
- IR final en fase mínima vía cepstrum (2048 taps @ 48 kHz).

Validación absoluta (datasheet → sonido, sin ancla, 1/3-oct 100 Hz-10 kHz):
4x12 closed ~2.1 dB; 2x12 open-back ~3.0 dB vs IRs medidos de referencia.

Todo en <1 ms por render — se puede regenerar al arrastrar el mic en la UI.
"""

import numpy as np
from scipy.special import j1
from scipy.io import wavfile

C_SOUND = 343.0  # m/s


# ── micrófonos (curvas desde los specsheets oficiales, Cabs/mics/) ────────
def _bump(f, f0, gain_db, width_oct):
    return gain_db * np.exp(-0.5 * (np.log2(np.maximum(f, 1.0) / f0) / width_oct) ** 2)


def _lo_shelf(f, f0, gain_db, slope_oct=1.0):
    x = np.log2(np.maximum(f, 1.0) / f0) / slope_oct
    return gain_db * (1.0 / (1.0 + np.exp(x * 2.2)))


def _hi_shelf(f, f0, gain_db, slope_oct=1.0):
    return _lo_shelf(f, f0, -gain_db, slope_oct) + gain_db


def _sm57(f):
    return (_lo_shelf(f, 190.0, -9.0, 1.2) + _bump(f, 4300.0, 1.5, 0.9)
            + _bump(f, 5900.0, 5.0, 0.45) + _bump(f, 9500.0, 1.5, 0.4)
            + _hi_shelf(f, 13000.0, -14.0, 0.5))


def _tlm103(f):
    return (_lo_shelf(f, 45.0, -3.0, 0.8) + _bump(f, 11000.0, 4.0, 0.75)
            + _hi_shelf(f, 19000.0, -6.0, 0.4))


def _md421(f):
    return (_lo_shelf(f, 110.0, -5.0, 1.0) + _bump(f, 4700.0, 4.0, 0.9)
            + _hi_shelf(f, 15000.0, -10.0, 0.5))


def _km84(f):
    return (_lo_shelf(f, 38.0, -3.0, 0.7) + _bump(f, 9000.0, 1.5, 0.7)
            + _hi_shelf(f, 18000.0, -5.0, 0.4))


def _r121(f):
    return (_bump(f, 55.0, 2.0, 0.8) + _bump(f, 6000.0, -2.8, 1.0)
            + _bump(f, 12500.0, 1.0, 0.4) + _hi_shelf(f, 15000.0, -8.0, 0.5))


def _tube(f):
    """U47-style PROVISIONAL (specsheet en Cabs/mics/ pendiente de digitalizar):
    condensador cálido — leve low-mid, top suavizado vs TLM103."""
    return (_lo_shelf(f, 45.0, -2.0, 0.8) + _bump(f, 180.0, 1.0, 1.0)
            + _bump(f, 8000.0, 2.0, 0.8) + _hi_shelf(f, 14000.0, -7.0, 0.5))


# nombre → (curva, proximity_strength dB @2.5 cm nominal, patrón)
MICS = {
    "sm57":   (_sm57,   30.0, "cardioid"),
    "tlm103": (_tlm103, 10.0, "cardioid"),
    "md421":  (_md421,  26.0, "cardioid"),
    "km84":   (_km84,   18.0, "cardioid"),
    "r121":   (_r121,   38.0, "fig8"),
    "tube":   (_tube,   12.0, "cardioid"),
}


# ── drivers (curvas SPL digitalizadas de los datasheets, Cabs/speakers/) ──
_G12M_POINTS = [
    (50, 88.0), (63, 92.0), (75, 94.5), (100, 95.0), (150, 95.5), (200, 95.0),
    (300, 94.5), (400, 95.0), (500, 96.0), (700, 97.0), (900, 98.0),
    (1200, 99.5), (1600, 101.0), (2000, 102.0), (2500, 103.5), (3000, 104.0),
    (3600, 103.0), (4200, 102.5), (4800, 103.0), (5500, 103.0),
    (6500, 90.0), (7500, 80.0), (8500, 75.0), (10000, 73.0), (12000, 74.0),
    (15000, 70.0), (20000, 65.0),
]
_BLUE_POINTS = [
    (50, 88.0), (63, 92.5), (75, 95.0), (100, 97.0), (150, 97.5), (200, 97.0),
    (300, 98.0), (400, 98.5), (500, 99.0), (700, 99.5), (900, 99.0),
    (1200, 100.5), (1600, 102.0), (2000, 103.5), (2500, 104.5), (3000, 104.0),
    (3600, 104.5), (4200, 104.0), (4800, 104.0), (5200, 100.0),
    (6500, 89.0), (7500, 84.0), (8500, 80.0), (10000, 78.0), (12000, 77.0),
    (15000, 72.0), (20000, 66.0),
]


# Celestion Vintage 30 (speakers/Celestion_Vintage_30.pdf). Fs=75, 100 dB.
# La firma V30: mid-range "vocal" 1.6-2.5k + pico fuerte 4.5-5k, cliff >5.2k.
_V30_POINTS = [
    (50, 86.0), (63, 90.0), (75, 94.5), (100, 95.5), (150, 96.0), (200, 95.5),
    (300, 96.5), (400, 97.0), (500, 97.5), (700, 98.5), (900, 99.5),
    (1200, 100.5), (1600, 102.0), (2000, 103.0), (2400, 103.5), (2800, 102.0),
    (3300, 100.5), (3800, 101.5), (4300, 104.0), (4800, 105.5), (5300, 103.0),
    (6500, 88.0), (7500, 80.0), (8500, 77.0), (10000, 75.0), (12000, 76.0),
    (15000, 71.0), (20000, 66.0),
]
# Celestion G12T-75 (speakers/Celestion_G12T-75.pdf). Fs=85, 97 dB.
# El scoop de medios clásico (500-900) + doble pico 3-4.5k + top suavizado.
_G12T75_POINTS = [
    (50, 84.0), (63, 88.0), (85, 94.0), (100, 95.0), (150, 95.5), (200, 95.0),
    (300, 94.5), (400, 94.0), (500, 93.5), (700, 93.0), (900, 93.5),
    (1200, 95.0), (1600, 97.5), (2000, 99.5), (2500, 101.5), (3000, 104.0),
    (3600, 105.5), (4200, 105.0), (4800, 103.0), (5400, 99.0),
    (6500, 86.0), (7500, 78.0), (8500, 74.0), (10000, 72.0), (12000, 73.0),
    (15000, 69.0), (20000, 64.0),
]
# Celestion G12H Anniversary (speakers/Celestion_G12H_Anniversary_1.pdf).
# Fs=85, 100 dB. Low-mids potentes + upper-mid atacante, "ice-cool top".
_G12H_POINTS = [
    (50, 87.0), (63, 91.0), (85, 95.5), (100, 96.5), (150, 97.0), (200, 96.5),
    (300, 96.5), (400, 96.5), (500, 97.0), (700, 97.5), (900, 98.5),
    (1200, 100.0), (1600, 102.0), (2000, 103.5), (2500, 104.5), (3000, 104.0),
    (3600, 104.5), (4200, 104.0), (4800, 103.5), (5400, 100.0),
    (6500, 88.0), (7500, 80.0), (8500, 76.0), (10000, 74.0), (12000, 75.0),
    (15000, 70.0), (20000, 65.0),
]
# Jensen C12N (speakers/JENSEN_C12N_ENG.pdf, curva 8Ω en baffle IEC).
# Fs=112 (!) — graves apretados americanos; brillo 2.5k y top más extendido
# que los británicos (el cliff llega ~1 octava más arriba).
_C12N_POINTS = [
    (60, 78.0), (80, 88.0), (100, 96.0), (120, 99.5), (150, 100.0), (200, 99.0),
    (300, 98.0), (400, 97.5), (500, 97.5), (700, 98.0), (900, 98.5),
    (1200, 99.5), (1600, 101.0), (2000, 103.5), (2500, 105.5), (3000, 104.0),
    (3500, 101.0), (4000, 103.0), (4500, 104.5), (5200, 103.0), (6000, 100.0),
    (7000, 92.0), (8000, 84.0), (9000, 80.0), (10000, 78.0), (12000, 79.0),
    (15000, 73.0), (20000, 67.0),
]


def _interp_points(points, f):
    pf = np.array([p[0] for p in points], float)
    pd = np.array([p[1] for p in points], float)
    db = np.interp(np.log10(np.maximum(f, 1.0)), np.log10(pf), pd)
    return db - np.mean(pd[(pf >= 150) & (pf <= 400)])


DRIVERS = {
    "g12m": lambda f: _interp_points(_G12M_POINTS, f),
    "blue": lambda f: _interp_points(_BLUE_POINTS, f),
    "v30": lambda f: _interp_points(_V30_POINTS, f),
    "g12t75": lambda f: _interp_points(_G12T75_POINTS, f),
    "g12h": lambda f: _interp_points(_G12H_POINTS, f),
    "c12n": lambda f: _interp_points(_C12N_POINTS, f),
}


# ── geometría del driver ─────────────────────────────────────────────────
def speaker_geom(size_in=12.0):
    """(radio_cono, radio_dustcap, profundidad) escalados por tamaño."""
    s = size_in / 12.0
    return 0.135 * s, 0.036 * s, 0.052 * s


# ── parámetros del modelo de posición (calibrados, ver prototipo) ────────
POSITION_PARAMS = {
    "w_dc_center": 0.72, "w_mid_center": 0.22, "w_ed_center": 0.06,
    "w_dc_edge": 0.04, "w_mid_edge": 0.32, "w_ed_edge": 0.64,
    "fc_dustcap": 10500.0, "fc_mid": 2000.0, "fc_edge": 1600.0,
    "coherence": 0.35,
    "f_breakup": 5150.0, "breakup_edge_db": 4.5, "breakup_width_oct": 0.35,
    "f_decouple": 1500.0, "p_decouple": 0.85,
}


def _position_db(f, x_norm, dist, size_in):
    a, a_dc, depth = speaker_geom(size_in)
    p = POSITION_PARAMS
    xm = np.clip(x_norm, 0.0, 1.0) * a
    x = np.clip(x_norm, 0.0, 1.0)
    w_dc = p["w_dc_center"] * (1 - x) + p["w_dc_edge"] * x
    w_mid = p["w_mid_center"] * (1 - x) + p["w_mid_edge"] * x
    w_ed = p["w_ed_center"] * (1 - x) + p["w_ed_edge"] * x

    zones = [(w_dc, np.hypot(xm, dist + depth), p["fc_dustcap"])]
    for lat in (abs(xm - 0.55 * a), xm + 0.55 * a):
        zones.append((0.5 * w_mid, np.hypot(lat, dist + 0.55 * depth), p["fc_mid"]))
    for lat in (abs(xm - 0.95 * a), xm + 0.95 * a):
        zones.append((0.5 * w_ed, np.hypot(lat, dist + 0.08 * depth), p["fc_edge"]))

    k = 2.0 * np.pi * f / C_SOUND
    H_c = np.zeros_like(f, dtype=complex)
    P_i = np.zeros_like(f)
    lf = 0.0
    for w, r, fc in zones:
        mag = w * 10.0 ** (-10.0 * 2 * np.log10(1.0 + (f / fc) ** 2) / 20.0)
        mag = mag * (0.05 / max(r, 0.005))
        H_c += mag * np.exp(-1j * k * r)
        P_i += mag ** 2
        lf += w * (0.05 / max(r, 0.005))
    coh = p["coherence"]
    mag_mix = (coh * np.abs(H_c) + (1 - coh) * np.sqrt(P_i)) / max(lf, 1e-9)
    bk = p["breakup_edge_db"] * x ** 1.5 * np.exp(
        -0.5 * (np.log2(np.maximum(f, 1.0) / p["f_breakup"]) / p["breakup_width_oct"]) ** 2)
    return 20.0 * np.log10(np.maximum(mag_mix, 1e-9)) + bk


def _directivity_db(f, theta_rad, dist, size_in):
    if abs(theta_rad) < 1e-4:
        return np.zeros_like(f)
    a = speaker_geom(size_in)[0]
    p = POSITION_PARAMS
    a_eff = a / (1.0 + (f / p["f_decouple"]) ** p["p_decouple"])
    ka = np.maximum(2.0 * np.pi * f / C_SOUND * a_eff * np.sin(abs(theta_rad)), 1e-9)
    db = 20.0 * np.log10(np.maximum(np.abs(2.0 * j1(ka) / ka), 1e-4))
    return db * (dist / (dist + a))       # suavizado de campo cercano


def _proximity_db(f, dist, mic):
    curve, strength, pattern = MICS[mic]
    if strength <= 0.0:
        return np.zeros_like(f)
    d_eff = np.hypot(max(dist, 0.005), 0.08)   # campo cercano fuente grande
    gain = strength * (0.025 / d_eff)
    return gain / (1.0 + np.exp(np.log2(np.maximum(f, 1.0) / 250.0) * 2.0))


def _enclosure_db(f, drivers=4, size_in=12.0, back="closed", baffle_m=0.76):
    d = np.zeros_like(f)
    f_baffle = C_SOUND / (np.pi * max(baffle_m, 0.2))
    x = np.log2(np.maximum(f, 1.0) / f_baffle)
    d += 6.0 / (1.0 + np.exp(-x * 1.8)) - 6.0
    if back == "closed":
        # bloom medido en el 4x12 (+11 dB <550); escala ±3 dB por duplicación
        bloom = 11.0 + 3.0 * np.log2(max(drivers, 1) / 4.0)
        d += bloom / (1.0 + np.exp(np.log2(np.maximum(f, 1.0) / 550.0) * 1.6))
    else:
        # open-back: bloom menor + cancelación dipolo (medido en el AC30 2x12)
        bloom = 8.0 + 3.0 * np.log2(max(drivers, 1) / 2.0)
        d += bloom / (1.0 + np.exp(np.log2(np.maximum(f, 1.0) / 450.0) * 1.6))
        d -= 8.0 / (1.0 + np.exp(np.log2(np.maximum(f, 1.0) / 140.0) * 2.0))
    return d


# ── síntesis del IR ──────────────────────────────────────────────────────
def synthesize_ir(speaker="g12m", mic="sm57", x=0.15, dist_in=1.0,
                  angle_deg=0.0, drivers=4, size_in=12.0, back="closed",
                  baffle_m=0.76, sr=48000, n_fft=8192, n_out=2048):
    """Parámetros físicos → IR float32 fase-mínima listo para el engine."""
    f = np.fft.rfftfreq(n_fft, 1.0 / sr)
    f[0] = f[1]
    dist = float(dist_in) * 0.0254 + 0.015    # + offset de rejilla
    db = DRIVERS[speaker](f)
    db = db + _position_db(f, x, dist, size_in)
    db = db + _directivity_db(f, np.deg2rad(angle_deg), dist, size_in)
    db = db + MICS[mic][0](f) + _proximity_db(f, dist, mic)
    db = db + _enclosure_db(f, drivers, size_in, back, baffle_m)
    db -= np.max(db)

    mag = np.maximum(10.0 ** (db / 20.0), 1e-6)
    log_mag = np.log(np.concatenate([mag, mag[-2:0:-1]]))
    cep = np.fft.ifft(log_mag).real
    n = len(cep)
    lift = np.zeros(n)
    lift[0] = 1.0
    lift[1:n // 2] = 2.0
    lift[n // 2] = 1.0
    ir = np.fft.ifft(np.exp(np.fft.fft(cep * lift))).real[:n_out]
    fade = int(0.15 * n_out)
    ir[-fade:] *= np.hanning(2 * fade)[fade:]
    peak = np.max(np.abs(ir)) + 1e-12
    return (ir / peak * 0.89).astype(np.float32)


def synthesize_ir_wav(out_path, **kwargs):
    """Renderiza y guarda el IR como wav float32 (para el slot cabinet)."""
    sr = int(kwargs.get("sr", 48000))
    ir = synthesize_ir(**kwargs)
    wavfile.write(out_path, sr, ir)
    return out_path
