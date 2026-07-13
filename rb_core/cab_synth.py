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


# Jensen P10Q (speakers/Jensen P10Q.pdf, curva 8Ω en baffle IEC).
# 10" ALNICO del Fender Bassman 4x10. Fs≈90, 93.8 dB, cono 330 cm².
# Firma: hump 100-150 Hz, medios declinando suave, pico de presencia
# 105-107 @3.5-4k y caída con notches >4.5k (el sparkle alnico de 10").
_P10Q_POINTS = [
    (50, 65.0), (70, 74.0), (90, 92.0), (100, 100.0), (120, 101.0),
    (150, 100.5), (200, 98.5), (300, 97.5), (400, 97.0), (500, 96.5),
    (700, 95.5), (900, 95.0), (1200, 95.5), (1600, 96.0), (2000, 97.0),
    (2500, 100.0), (3000, 104.0), (3500, 106.0), (4000, 105.5), (4500, 102.0),
    (5000, 97.0), (6000, 88.0), (7000, 82.0), (8000, 85.0), (9000, 80.0),
    (10000, 78.0), (12000, 80.0), (15000, 74.0), (20000, 68.0),
]


# Jensen MOD 12-110 (speakers/MOD-12-110.pdf, curva 8Ω en baffle IEC).
# El 12" cerámico moderno para el Ronald JC-120 (CS212C, identificado por el
# usuario). Fs=85.6, 99.1 dB. Firma: pico de resonancia ~95-100 Hz, leve
# valle 600-900, montaña de presencia 2.5-4k (~106) y cliff ~6k con notches.
_MOD12110_POINTS = [
    (50, 72.0), (70, 80.0), (90, 99.0), (100, 101.0), (120, 99.5),
    (150, 99.5), (200, 99.0), (300, 97.5), (400, 97.0), (500, 96.5),
    (700, 95.5), (900, 96.0), (1200, 98.5), (1600, 99.5), (2000, 101.0),
    (2500, 104.5), (3000, 106.0), (3500, 105.5), (4200, 104.0), (5000, 104.0),
    (5800, 100.0), (6500, 90.0), (7500, 82.0), (8500, 80.0), (10000, 76.0),
    (12000, 78.0), (15000, 74.0), (20000, 70.0),
]


# Electro-Voice EVM-15L Pro-Line (speakers/EVM-15L Pro-line EDS.pdf, curva
# en caja vented TL606). Fs=43 (free-air), Sd=855 cm². Plano y extendido
# 80-1k, presencia suave 3-4k y cliff ~4.5k — el 15" full-range de EV.
_EVM15L_POINTS = [
    (50, 80.0), (60, 86.0), (80, 90.0), (100, 91.0), (150, 92.0), (200, 92.0),
    (300, 92.0), (400, 92.5), (500, 92.5), (700, 93.0), (900, 93.0),
    (1200, 93.5), (1600, 94.0), (2000, 94.5), (2500, 95.5), (3000, 96.5),
    (3500, 97.0), (4200, 96.0), (5000, 92.0), (6000, 84.0), (7000, 79.0),
    (8000, 76.0), (10000, 73.0), (12000, 72.0), (15000, 69.0), (20000, 65.0),
]
# Electro-Voice EVM12L (speakers/EVM12L Engineering Data Sheet.pdf, curva
# 1W/1m en caja vented 1.3 ft³). El "classic lead guitar" de EV: 100 dB,
# Fs=55, meseta alta 300-1k, punch 2-3k (~105) y cliff ~5.5k sin fizz.
_EVM12L_POINTS = [
    (60, 84.0), (80, 89.0), (100, 93.0), (150, 97.0), (200, 99.0),
    (300, 100.0), (400, 100.5), (500, 101.0), (700, 101.5), (900, 102.0),
    (1200, 102.5), (1600, 103.5), (2000, 104.5), (2500, 105.0), (3000, 104.5),
    (3500, 103.5), (4200, 104.0), (5000, 105.0), (5500, 102.0), (6500, 90.0),
    (7500, 80.0), (8500, 75.0), (10000, 72.0), (12000, 74.0), (15000, 73.0),
    (20000, 68.0),
]



# ── drivers de BAJO (datasheets oficiales, Cabs/speakers/) ────────────────
# Eminence Legend BP102 (10", el clásico de los 8x10/4x10 vintage tipo SVT).
# Fs=35, 91.8 dB, usable 40 Hz-2 kHz — OSCURO, cliff ~2.5k.
_BP102_POINTS = [
    (40, 78.0), (60, 86.0), (80, 89.0), (100, 90.0), (150, 90.5), (200, 90.5),
    (300, 91.0), (400, 91.5), (500, 91.0), (700, 90.0), (900, 90.0),
    (1200, 91.0), (1600, 92.0), (2000, 92.0), (2500, 89.0), (3000, 84.0),
    (4000, 74.0), (5000, 66.0), (6000, 62.0), (8000, 58.0), (10000, 56.0),
    (15000, 52.0), (20000, 50.0),
]
# Eminence Legend CB158 (15" clásico). Fs=34, 98.2 dB, 47 Hz-3 kHz,
# montaña de presencia ~2 kHz y cliff ~3k.
_CB158_POINTS = [
    (40, 82.0), (60, 90.0), (80, 94.0), (100, 95.0), (150, 95.5), (200, 95.5),
    (300, 95.0), (400, 94.5), (500, 94.0), (700, 94.0), (900, 95.0),
    (1200, 97.0), (1600, 99.5), (2000, 101.5), (2500, 101.0), (3000, 97.0),
    (3500, 90.0), (4000, 83.0), (5000, 73.0), (6000, 68.0), (8000, 63.0),
    (10000, 60.0), (15000, 55.0), (20000, 52.0),
]
# Eminence Deltalite II 2510 (10" neo moderno, Eden/SWR). Fs=53, 97.3 dB,
# 60 Hz-4 kHz — más brillante, pico 2-2.5k.
_DL2510_POINTS = [
    (50, 84.0), (70, 91.0), (100, 95.0), (150, 95.5), (200, 95.5),
    (300, 95.5), (500, 96.0), (700, 96.0), (900, 96.5), (1200, 97.5),
    (1600, 99.5), (2000, 102.0), (2400, 102.5), (2800, 101.0), (3200, 100.0),
    (4000, 96.0), (5000, 93.0), (6000, 85.0), (7000, 79.0), (8000, 76.0),
    (10000, 73.0), (12000, 70.0), (15000, 66.0), (20000, 60.0),
]
# Eminence Deltalite II 2512 (12" neo). Fs=44, 99.9 dB, 58 Hz-4.3 kHz.
_DL2512_POINTS = [
    (45, 84.0), (70, 92.0), (100, 96.0), (150, 96.5), (200, 96.5),
    (300, 96.5), (500, 97.0), (700, 97.0), (900, 97.5), (1200, 98.5),
    (1600, 100.5), (2000, 103.5), (2400, 104.5), (2800, 103.0), (3300, 103.5),
    (4000, 100.0), (4800, 88.0), (5500, 76.0), (6500, 73.0), (8000, 78.0),
    (9000, 77.0), (10000, 72.0), (15000, 64.0), (20000, 58.0),
]
# Jensen P15N No Bell (15" ALNICO vintage — el sonido B-15/Bassman 15).
# Fs=77, 96.8 dB, con top de verdad (voz de guitarra/bajo vintage).
_P15N_POINTS = [
    (50, 72.0), (70, 84.0), (90, 96.0), (110, 100.0), (150, 99.0),
    (200, 98.5), (300, 98.0), (400, 97.0), (500, 96.5), (700, 97.0),
    (900, 98.5), (1200, 100.0), (1600, 102.5), (2000, 104.5), (2500, 106.0),
    (3000, 105.0), (3500, 103.0), (4200, 100.0), (5000, 90.0), (6000, 82.0),
    (7000, 80.0), (8000, 83.0), (10000, 86.0), (12000, 78.0), (15000, 72.0),
    (20000, 64.0),
]
# Jensen C15N (15" cerámico vintage). Fs=73, 96.3 dB, presencia 2-3k enorme.
_C15N_POINTS = [
    (50, 66.0), (70, 78.0), (100, 92.0), (150, 99.0), (200, 100.0),
    (300, 100.5), (400, 100.5), (500, 100.0), (700, 97.0), (900, 98.0),
    (1200, 99.5), (1600, 102.0), (2000, 105.0), (2500, 107.0), (3000, 106.0),
    (3500, 105.0), (4200, 92.0), (5000, 95.0), (6000, 96.0), (7000, 88.0),
    (8000, 78.0), (10000, 75.0), (15000, 70.0), (20000, 65.0),
]
# Celestion Pulse 15 (15" bass moderno, Kevlar). Fs=35.7, 96 dB, 40-3500 Hz.
_PULSE15_POINTS = [
    (40, 80.0), (60, 86.0), (80, 89.0), (100, 90.0), (150, 92.0), (200, 93.0),
    (300, 93.5), (400, 94.0), (500, 94.5), (700, 95.0), (900, 95.5),
    (1200, 97.0), (1600, 100.0), (2000, 103.5), (2500, 102.0), (3000, 100.0),
    (3500, 93.0), (4000, 82.0), (5000, 74.0), (6000, 72.0), (8000, 70.0),
    (10000, 68.0), (15000, 62.0), (20000, 58.0),
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
    "p10q": lambda f: _interp_points(_P10Q_POINTS, f),
    "mod12110": lambda f: _interp_points(_MOD12110_POINTS, f),
    "evm15l": lambda f: _interp_points(_EVM15L_POINTS, f),
    "evm12l": lambda f: _interp_points(_EVM12L_POINTS, f),
    "bp102": lambda f: _interp_points(_BP102_POINTS, f),
    "cb158": lambda f: _interp_points(_CB158_POINTS, f),
    "deltalite2510": lambda f: _interp_points(_DL2510_POINTS, f),
    "deltalite2512": lambda f: _interp_points(_DL2512_POINTS, f),
    "p15n": lambda f: _interp_points(_P15N_POINTS, f),
    "c15n": lambda f: _interp_points(_C15N_POINTS, f),
    "pulse15": lambda f: _interp_points(_PULSE15_POINTS, f),
}


# ── geometría del driver ─────────────────────────────────────────────────
def speaker_geom(size_in=12.0):
    """(radio_cono, radio_dustcap, profundidad) escalados por tamaño."""
    s = size_in / 12.0
    return 0.135 * s, 0.036 * s, 0.052 * s


# ── parámetros del modelo de posición (calibrados, ver prototipo) ────────
POSITION_PARAMS = {
    # Centre→edge zone weights, COMPRESSED ~30% (user: centre was "mucho más agudo"
    # than the edge). The direction is physically right — the bright dust-cap (fc
    # 9.8 kHz) dominates on-axis, the dark cone edge (fc 1.8 kHz) off it — but the
    # spread was too extreme; softened so it's a real, gentler treble gradient.
    # Weights still sum to 1.0 per position. To restore the old aggressive contrast,
    # revert to 0.72/0.22/0.06 → 0.04/0.32/0.64 with fc 10500/1600.
    "w_dc_center": 0.62, "w_mid_center": 0.25, "w_ed_center": 0.13,
    "w_dc_edge": 0.14, "w_mid_edge": 0.33, "w_ed_edge": 0.53,
    "fc_dustcap": 9800.0, "fc_mid": 2000.0, "fc_edge": 1800.0,
    "coherence": 0.35,
    "f_breakup": 5150.0, "breakup_edge_db": 3.5, "breakup_width_oct": 0.35,
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
    elif back == "ported":
        # bass-reflex: bloom de caja + HUMP del puerto (~55 Hz) + rolloff
        # subsónico bajo la sintonía (el puerto descarga el cono)
        bloom = 10.0 + 3.0 * np.log2(max(drivers, 1) / 4.0)
        d += bloom / (1.0 + np.exp(np.log2(np.maximum(f, 1.0) / 500.0) * 1.6))
        d += 3.5 * np.exp(-0.5 * (np.log2(np.maximum(f, 1.0) / 55.0) / 0.5) ** 2)
        d -= 10.0 / (1.0 + np.exp(np.log2(np.maximum(f, 1.0) / 38.0) * 3.0))
    else:
        # open-back: bloom menor + cancelación dipolo (medido en el AC30 2x12)
        bloom = 8.0 + 3.0 * np.log2(max(drivers, 1) / 2.0)
        d += bloom / (1.0 + np.exp(np.log2(np.maximum(f, 1.0) / 450.0) * 1.6))
        d -= 8.0 / (1.0 + np.exp(np.log2(np.maximum(f, 1.0) / 140.0) * 2.0))
    return d


# ── síntesis del IR ──────────────────────────────────────────────────────
# ── cabs NOVELTY (radio/gramófono/jukebox/boombox/hi-fi/PA) ───────────────
# No son cabs de parlante con datasheet: se modelan como CURVAS DE VOICING
# (bandpass del arquetipo + resonancias características). El bandpass usa
# skirts tipo Butterworth; los bumps dan el "honk" de bocina / la caja de
# madera / el hype barato del boombox. `mic` sigue teniendo un efecto leve
# (0.35×) para que el Cab Room mueva algo, pero aquí no hay mic real.
def _hp_db(f, fc, order=2):
    r = (np.maximum(f, 1.0) / fc) ** (2 * order)
    return 10.0 * np.log10(r / (1.0 + r))


def _lp_db(f, fc, order=2):
    r = (np.maximum(f, 1.0) / fc) ** (2 * order)
    return 10.0 * np.log10(1.0 / (1.0 + r))


VOICINGS = {
    # bocina acústica: banda muy angosta 250-3k, honk peaky ~700/1.8k, papelosa
    "gramophone": lambda f: (_hp_db(f, 260, 3) + _lp_db(f, 3000, 3)
        + _bump(f, 700, 7.0, 0.35) + _bump(f, 1800, 6.0, 0.4)
        + _bump(f, 350, -4.0, 0.5)),
    # radio de consola a tubos, parlante ~6" en mueble de madera
    "cabinetradio": lambda f: (_hp_db(f, 170, 2) + _lp_db(f, 4200, 2)
        + _bump(f, 220, 3.0, 0.6) + _bump(f, 1500, 4.0, 0.7)
        + _bump(f, 3200, -3.0, 0.5)),
    # jukebox: caja grande, más grave y cálido, agudos rodados
    "jukebox": lambda f: (_hp_db(f, 90, 2) + _lp_db(f, 6000, 2)
        + _bump(f, 120, 4.0, 0.5) + _bump(f, 900, 2.5, 0.8)
        + _bump(f, 4500, -3.0, 0.6)),
    # boombox 80s: graves porteados hinchados, medios scoopeados, presencia dura
    "boombox": lambda f: (_hp_db(f, 80, 3) + _lp_db(f, 10000, 2)
        + _bump(f, 100, 5.0, 0.4) + _bump(f, 550, -5.0, 0.8)
        + _bump(f, 3500, 5.5, 0.6) + _bump(f, 8000, 3.0, 0.5)),
    # ── los 5 "serios" anclados a datasheets REALES (Cabs/speakers/) ──────
    # Audiophile Hi-Fi: 2-vías moderno neutral, plano 40 Hz-20 kHz.
    "audiophile": lambda f: _interp_points(_HIFI_MODERN_POINTS, f),
    # Vintage Hi-Fi = JBL D123 (12" full-range alnico, dome dural, Fs 35,
    # usable 30-15k): cálido, suave, agudos extendidos que ruedan tras ~9k.
    "vintagehifi": lambda f: _interp_points(_HIFI_D123_POINTS, f),
    # PS-600 PA = JBL SRX812 (12"+horn, -3dB 57 Hz-20 kHz, xover 1.9k):
    # PA moderno ULTRA-plano, meseta pareja, extendido hasta 20k.
    "pa600c": lambda f: _interp_points(_PA_JBL12_POINTS, f),
    # PA-999C = Mc Crypt PA 15/2A (15"+horn, curva MEDIDA 0°): PA económico
    # con bump de graves ~70, dip de crossover ~800 y pico de horn ~10k.
    "pa999c": lambda f: _interp_points(_PA_BUDGET15_POINTS, f),
    # PS-115.2C = NEXO PS15 (15"+2" HF, el parlante real que parodia): PA
    # premium de gira, muy plano con brillo de horn suave arriba.
    "pa1152c": lambda f: _interp_points(_PA_NEXO15_POINTS, f),
}

# JBL D123 12" full-range alnico (D123.jpg) — Fs 35, dome dural, 30-15k
_HIFI_D123_POINTS = [
    (30, 74), (40, 84), (50, 90), (60, 93), (80, 95), (100, 96), (150, 96),
    (200, 95.5), (300, 95), (500, 95), (700, 95), (1000, 95.5), (1500, 96),
    (2000, 96.5), (3000, 97), (4000, 97), (5000, 96.5), (7000, 95), (9000, 93),
    (11000, 90), (13000, 86), (15000, 82), (18000, 74), (20000, 68),
]
# hi-fi 2-vías moderno neutral (referencia audiophile plana)
_HIFI_MODERN_POINTS = [
    (30, 80), (40, 90), (50, 94), (60, 95), (80, 96), (100, 96), (200, 96),
    (500, 96), (1000, 96), (2000, 96), (3000, 96.5), (5000, 96.5), (7000, 96),
    (10000, 96), (13000, 95.5), (16000, 95), (18000, 93), (20000, 90),
]
# JBL SRX812 12"+horn (JBL_SRX812) — -3dB 57 Hz-20 kHz, meseta plana ~93
_PA_JBL12_POINTS = [
    (40, 72), (50, 84), (57, 90), (70, 92), (100, 93), (150, 93), (200, 93),
    (300, 92.5), (500, 92.5), (700, 92.5), (1000, 92.5), (1500, 92.5),
    (1900, 92.5), (2500, 93), (3500, 92.5), (5000, 93), (7000, 93),
    (10000, 93.5), (13000, 93), (16000, 92), (18000, 91), (20000, 89),
]
# Mc Crypt PA 15/2A 15"+horn — leído de la curva MEDIDA 0° del datasheet
_PA_BUDGET15_POINTS = [
    (20, 68), (30, 78), (40, 88), (50, 96), (60, 100), (70, 101), (80, 101),
    (100, 100), (150, 100), (200, 98), (300, 99), (400, 98.5), (500, 98),
    (700, 97), (800, 96), (1000, 97), (1500, 99), (2000, 100), (2500, 98.5),
    (3000, 99), (4000, 100), (5000, 99), (7000, 100), (10000, 103),
    (12000, 102), (15000, 98), (18000, 96), (20000, 95),
]
# NEXO PS15 15"+2" HF (PS15-R2) — PA premium de gira, plano y extendido
_PA_NEXO15_POINTS = [
    (40, 78), (50, 90), (60, 97), (70, 99), (80, 100), (100, 100), (150, 100),
    (200, 100), (300, 99.5), (500, 99.5), (700, 99), (1000, 99), (1500, 99.5),
    (2000, 100), (3000, 100), (4000, 100.5), (5000, 100.5), (7000, 101),
    (10000, 101), (13000, 100), (16000, 98), (18000, 95), (20000, 90),
]


def synthesize_ir(speaker="g12m", mic="sm57", x=0.15, dist_in=1.0,
                  angle_deg=0.0, drivers=4, size_in=12.0, back="closed",
                  baffle_m=0.76, voicing=None, sr=48000, n_fft=8192, n_out=2048):
    """Parámetros físicos → IR float32 fase-mínima listo para el engine.

    Si `voicing` está en VOICINGS (cabs novelty), se usa esa curva de
    carácter en vez del motor driver+caja; el mic aporta un tilt leve."""
    f = np.fft.rfftfreq(n_fft, 1.0 / sr)
    f[0] = f[1]
    dist = float(dist_in) * 0.0254 + 0.015    # + offset de rejilla
    if voicing and voicing in VOICINGS:
        db = VOICINGS[voicing](f) + 0.35 * MICS[mic][0](f)
    else:
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
