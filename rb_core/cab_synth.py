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

import re

import numpy as np
from scipy.special import j1
from scipy.io import wavfile

C_SOUND = 343.0  # m/s

_ENCLOSURE_DSP_KEYS = (
    "width_m", "height_m", "depth_m", "volume_l", "panel_thickness_m",
    "tuning_hz", "dipole_hz", "open_back_ratio", "low_cut_hz",
    "cab_resonance_hz", "hf_kind", "hf_crossover_hz", "hf_level_db",
    "hf_rolloff_hz", "system_f3_hz", "system_f6_hz", "system_f10_hz",
)


def enclosure_kwargs(entry):
    """Extract only enclosure fields understood by ``synthesize_ir``."""
    return {
        key: entry[key]
        for key in _ENCLOSURE_DSP_KEYS
        if entry.get(key) is not None
    }


def _cache_slug(value):
    value = str(value or "").strip().replace(".", "")
    value = re.sub(r"[^0-9A-Za-z_-]+", "_", value)
    return re.sub(r"_+", "_", value).strip("_")


def cache_filename(base_gear, entry, speaker, mic, x, dist_in, angle_deg):
    """Stable cache key for every parameter that changes a synthesized IR."""
    gear_key = _cache_slug(base_gear) or "cab"
    speaker_key = _cache_slug(speaker) or "speaker"
    voicing_key = _cache_slug(entry.get("voicing") or "physical")
    back = str(entry.get("back", "closed"))
    back_key = {"closed": "c", "open": "o", "ported": "p"}.get(
        back, _cache_slug(back) or "x")
    drivers = int(entry.get("drivers", 1))
    size = f"{float(entry.get('size_in', 12)):g}".replace(".", "p")
    baffle_mm = int(round(float(entry.get("baffle_m", 0.6)) * 1000.0))
    geometry = []
    for key in _ENCLOSURE_DSP_KEYS:
        value = entry.get(key)
        if value is None:
            continue
        if isinstance(value, float):
            value = f"{value:.6g}"
        geometry.append(f"{key[:2]}{value}")
    geometry_key = _cache_slug("_".join(geometry)) or "generic"
    return (
        f"realcab_{gear_key}_{speaker_key}_{drivers}x{size}{back_key}"
        f"_b{baffle_mm:04d}_g{geometry_key}_v{voicing_key}_{mic}"
        f"_x{int(round(x * 100)):03d}_d{int(round(dist_in * 10)):03d}"
        f"_a{int(round(angle_deg)):02d}.wav"
    )


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
    """Calibrado vs grilla Redwirez (delta 421−SM57 @ Cap 1", err 1.3 dB RMS):
    el 421 real es MÁS FLACO que el 57 en graves (−1.4 @100) y mucho más
    brillante arriba (+8.7 @8k — su top se extiende donde el 57 ya cayó).
    El modelo viejo lo tenía gordo (+3.5) y oscuro (−2.2 @8k)."""
    return (_lo_shelf(f, 150.0, -7.0, 1.1) + _bump(f, 1200.0, 3.0, 1.2)
            + _bump(f, 4700.0, 4.0, 0.9) + _bump(f, 9500.0, 13.0, 0.5)
            + _hi_shelf(f, 15000.0, -6.0, 0.5))


def _km84(f):
    """Calibrado vs grilla Redwirez (delta KM84−SM57 @ Cap 1", err 1.5 dB RMS):
    cuerpo suave, dip en 6k y aire conservado en 8-9k (+3.6 real vs −1.6 del
    modelo viejo)."""
    return (_lo_shelf(f, 38.0, -3.0, 0.7) + _bump(f, 6000.0, -3.0, 0.9)
            + _bump(f, 9000.0, 7.0, 0.55) + _hi_shelf(f, 18000.0, -7.0, 0.4))


def _r121(f):
    """Calibrado contra la grilla Redwirez 1960A (delta R121−SM57 @ Cap 1",
    fit numérico err 1.1 dB RMS): el modelo viejo era una caricatura — doble
    proximidad (+13.8 dB @100 vs +7.3 real) y top enterrado (−4.5 @8k vs +2.0
    real: el R121 real conserva aire en 8-10k; su oscuridad es el dip 4-6k)."""
    return (_bump(f, 6000.0, -2.2, 1.0) + _bump(f, 9500.0, 5.5, 0.5)
            + _bump(f, 12500.0, 1.0, 0.4) + _hi_shelf(f, 15000.0, -4.0, 0.5))


def _tube(f):
    """Neumann U47 cardioide, digitalizado del datasheet Gotham/Neumann
    (Cabs/mics/Neumann-U-47-Microphone.pdf p.2, curva ♡; fit 0.40 dB RMS
    sobre 19 puntos 40 Hz-14 kHz): −2 dB @40, plano hasta ~1.3k, meseta de
    presencia +3.5..4 entre 3-10k con pico ~10k, caída fuerte sobre 12k
    (respuesta especificada 30-15,000 cps)."""
    return (_lo_shelf(f, 55.0, -2.8, 0.9) + _bump(f, 5800.0, 3.8, 1.2)
            + _bump(f, 10800.0, 2.2, 0.5) + _hi_shelf(f, 15000.0, -12.0, 0.6))


# Residuales anchos medidos siempre como delta contra el SM57 en el MISMO
# parlante, punto y distancia. Redwirez 1960A/G12M aporta el R121 y el pack
# AC30/Blue aporta el TLM103. Son correcciones de baja resolución: eliminan el
# color de la cadena de referencia y no incorporan combs ni el IR de terceros.
_MIC_CAL_FREQS = np.array([
    80.0, 100.0, 160.0, 250.0, 400.0, 630.0, 1000.0,
    1600.0, 2500.0, 4000.0, 6300.0, 8000.0, 10000.0, 12500.0,
])
_MIC_CAL_DB = {
    # AC30: recupera presencia útil 4-7 kHz. El antiguo boost a 11 kHz
    # quedaba detrás del cliff del parlante y el condensador sonaba tapado.
    "tlm103": np.array([
        -1.1, -2.7, -2.9, -1.7, -1.9, -2.2, -3.2,
        -1.2, 0.3, 0.5, 2.1, 0.2, -2.8, -2.5,
    ]),
    # Redwirez: mantiene el dip ribbon 5-7 kHz y devuelve cuerpo/presencia
    # 1-5 kHz. Top re-optimizado contra el barrido de 4 distancias (0.5-4",
    # err medio 1.37 dB, 0.80 @1"): el corte anterior de -6 dB @10-12.5k
    # enterraba el AIRE real del R121 (+2.0 @8k medido a 1") — su oscuridad
    # es el dip 5-7k, no una frazada; graves -1.5 (iban +1.7 sobre lo real).
    "r121": np.array([
        -6.9, -7.5, -7.5, -3.4, 0.0, 1.2, 1.7,
        0.8, 2.6, 3.1, 1.7, 3.0, 0.0, -3.0,
    ]),
}

# Residuo del R121 angulado 45 grados después de la directividad de pistón.
# Promedio de Cap/CapEdge @2" en la grilla Redwirez. En figura 8 el top no se
# apaga como en el modelo cardioide genérico; esta era otra fuente de oscuridad.
_R121_ANGLE_45_DB = np.array([
    -3.1, -3.2, -3.3, -2.8, -1.3, 0.1, -1.7,
    -1.6, -1.2, -1.6, -0.7, 2.3, 6.0, 4.5,
])


def _log_interp_db(f, freqs, values):
    return np.interp(
        np.log(np.maximum(f, freqs[0])), np.log(freqs), values,
        left=float(values[0]), right=float(values[-1]))


def _mic_calibration_db(f, mic):
    values = _MIC_CAL_DB.get(mic)
    if values is None:
        return np.zeros_like(f)
    return _log_interp_db(f, _MIC_CAL_FREQS, values)

# nombre → (curva, proximity_strength dB @2.5 cm nominal, patrón)
MICS = {
    "sm57":   (_sm57,   30.0, "cardioid"),
    "tlm103": (_tlm103, 10.0, "cardioid"),
    "md421":  (_md421,  26.0, "cardioid"),
    "km84":   (_km84,    6.0, "cardioid"),  # 18 lo engordaba +4 dB vs real (Redwirez fit)
    # El barrido 0.5-12" de Redwirez da 55.5 dB nominales para el R121. La
    # corrección estática anterior ocultaba esa dependencia con la distancia.
    "r121":   (_r121,   55.5, "fig8"),
    "tube":   (_tube,   12.0, "cardioid"),  # U47 digitalizado (ya no provisional)
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
# Celestion Seventy 80, originalmente G12P-80 (curva oficial 8 ohm).
# Fs=85 Hz, 98 dB. Mantiene graves controlados, valle ~1.4 kHz y una cresta
# agresiva 2.3-3.5 kHz antes de caer sobre 5.5 kHz.
_SEVENTY80_POINTS = [
    (20, 72.0), (30, 77.5), (40, 82.0), (50, 86.0), (63, 91.0),
    (80, 93.5), (100, 96.5), (150, 98.5), (200, 98.5), (300, 98.0),
    (400, 97.5), (500, 99.0), (700, 100.0), (850, 100.5), (1000, 99.0),
    (1200, 97.5), (1400, 94.5), (1600, 99.0), (1800, 102.5),
    (2000, 102.5), (2300, 103.5), (2600, 107.5), (3000, 106.0),
    (3300, 107.0), (3800, 104.0), (4300, 100.5), (5000, 99.0),
    (5500, 99.5), (6000, 94.5), (6500, 88.5), (7000, 86.0),
    (8000, 87.5), (9000, 83.0), (10000, 76.0), (11000, 75.0),
    (12000, 76.0), (14000, 79.5), (16000, 74.0), (18000, 66.0),
    (20000, 64.0),
]
# Celestion V-Type (curva oficial 8 ohm). Fs=75 Hz, 98 dB. Comparte la
# claridad del Seventy 80, pero con una cresta de presencia más suave y cuerpo
# medio más parejo, que es la voz documentada del VOX BC112.
_VTYPE_POINTS = [
    (20, 73.0), (30, 79.0), (40, 83.5), (50, 87.0), (63, 91.0),
    (80, 92.5), (100, 95.0), (150, 97.0), (200, 97.0), (300, 96.5),
    (400, 96.0), (500, 98.0), (700, 98.5), (850, 99.0), (1000, 97.5),
    (1200, 97.0), (1400, 93.0), (1600, 99.0), (1800, 100.5),
    (2000, 101.5), (2300, 105.5), (2600, 102.5), (3000, 102.0),
    (3300, 103.5), (3800, 101.5), (4300, 100.5), (5000, 98.0),
    (5500, 93.5), (6000, 87.5), (6500, 84.0), (7000, 80.5),
    (8000, 80.5), (9000, 80.5), (10000, 79.0), (11000, 79.5),
    (12000, 69.5), (14000, 78.0), (16000, 73.0), (18000, 74.0),
    (20000, 66.0),
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


def _proxy_blend(f, *weighted_points):
    """Blend documented donor curves for an unavailable proprietary driver.

    These are deliberately separate speaker IDs: a proxy must never silently
    claim to be the donor Eminence/Celestion model used to construct it.
    """
    total = sum(float(weight) for weight, _ in weighted_points)
    if total <= 0.0:
        raise ValueError("proxy blend requires a positive total weight")
    return sum(
        float(weight) * _interp_points(points, f)
        for weight, points in weighted_points
    ) / total


def _g12b150_proxy(f):
    # The discontinued G12B-150 has no recoverable manufacturer response plot.
    # Marshall documents it in the 1912; published Celestion specifications give
    # Fs 77 Hz, 60-4000 Hz, a 2-inch coil and a 50 oz ceramic magnet. Blend the
    # closest measured high-power 12-inch donors, then enforce its darker upper
    # range. This remains an explicit proxy until a measured curve/IR is found.
    return (_proxy_blend(
                f, (0.42, _EVM12L_POINTS), (0.33, _G12T75_POINTS),
                (0.25, _BP102_POINTS))
            + _bump(f, 360.0, 1.3, 0.85)
            + _bump(f, 1050.0, 0.8, 0.9)
            + _bump(f, 2800.0, -0.8, 0.55)
            + _lp_db(f, 4200.0, 1))


def _rumble10_proxy(f):
    # 2010 Fender Special Design 10: ferrite, 4 ohm, 125 W/driver. The BP102
    # supplies the older bass voicing and the 2510 restores usable upper mids.
    return (_proxy_blend(f, (0.68, _BP102_POINTS), (0.32, _DL2510_POINTS))
            + _bump(f, 850.0, 0.8, 0.75)
            + _bump(f, 2400.0, -0.7, 0.55))


def _gk_cx10_proxy(f):
    # CX410: four ceramic 10s, 200 W/driver. A broader upper-mid response than
    # the Rumble proxy matches the published 51 Hz-18 kHz system character.
    return (_proxy_blend(f, (0.42, _BP102_POINTS), (0.58, _DL2510_POINTS))
            + _bump(f, 1200.0, 0.9, 0.75)
            + _bump(f, 3100.0, 0.7, 0.55))


def _hydrive10_proxy(f):
    # Hartke's paper/aluminium inner cone is cleaner and more extended than a
    # conventional paper bass cone; the cabinet horn is modelled separately.
    return (_proxy_blend(f, (0.18, _BP102_POINTS), (0.82, _DL2510_POINTS))
            + _bump(f, 1700.0, 1.0, 0.75)
            + _bump(f, 3400.0, 0.9, 0.5))


def _hydrive12_proxy(f):
    return (_proxy_blend(f, (0.74, _DL2512_POINTS), (0.26, _EVM12L_POINTS))
            + _bump(f, 1500.0, 0.7, 0.8)
            + _bump(f, 3600.0, 0.8, 0.55))


def _hydrive15_proxy(f):
    return (_proxy_blend(f, (0.54, _PULSE15_POINTS), (0.46, _CB158_POINTS))
            + _bump(f, 1350.0, 0.8, 0.85)
            + _bump(f, 2850.0, 0.7, 0.55))


def _mesa_powerhouse15_proxy(f):
    # Custom ferrite Eminence: warm low mids with enough 2-4 kHz information
    # for the selectable 3/4/5 kHz Player Control crossover.
    return (_proxy_blend(f, (0.66, _CB158_POINTS), (0.34, _PULSE15_POINTS))
            + _bump(f, 520.0, 0.8, 0.75)
            + _bump(f, 2200.0, 0.6, 0.65))


def _mesa_subway15_proxy(f):
    # Lightweight custom neo driver: tighter and more extended than the older
    # PowerHouse unit, anchored to Mesa's published 2x15 system response.
    return (_proxy_blend(f, (0.72, _PULSE15_POINTS), (0.28, _CB158_POINTS))
            + _bump(f, 800.0, 0.7, 0.85)
            + _bump(f, 2600.0, 0.8, 0.6))


def _fane122231_proxy(f):
    # Hiwatt/Fane 122231, Fs 75 Hz and usable to 8 kHz. G12H supplies the
    # vintage cone breakup while EVM12L adds the flatter high-power character.
    return (_proxy_blend(f, (0.64, _G12H_POINTS), (0.36, _EVM12L_POINTS))
            + _bump(f, 950.0, 0.6, 0.85)
            + _bump(f, 3600.0, -0.8, 0.55))


def _fane_bass15_proxy(f):
    # Modern Fane Sovereign 15-400 is the documented donor family: ferrite,
    # Fs 37 Hz, 40 Hz-4 kHz. It is a proxy, not an identification of the OEM.
    return (_proxy_blend(f, (0.58, _CB158_POINTS), (0.42, _EVM15L_POINTS))
            + _bump(f, 900.0, 0.6, 0.8)
            + _bump(f, 2600.0, 0.7, 0.6))


DRIVERS = {
    "g12m": lambda f: _interp_points(_G12M_POINTS, f),
    "blue": lambda f: _interp_points(_BLUE_POINTS, f),
    "v30": lambda f: _interp_points(_V30_POINTS, f),
    "g12t75": lambda f: _interp_points(_G12T75_POINTS, f),
    "seventy80": lambda f: _interp_points(_SEVENTY80_POINTS, f),
    "vtype": lambda f: _interp_points(_VTYPE_POINTS, f),
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
    "g12b150_proxy": _g12b150_proxy,
    "rumble10_proxy": _rumble10_proxy,
    "gk_cx10_proxy": _gk_cx10_proxy,
    "hydrive10_proxy": _hydrive10_proxy,
    "hydrive12_proxy": _hydrive12_proxy,
    "hydrive15_proxy": _hydrive15_proxy,
    "mesa_powerhouse15_proxy": _mesa_powerhouse15_proxy,
    "mesa_subway15_proxy": _mesa_subway15_proxy,
    "fane122231_proxy": _fane122231_proxy,
    "fane_bass15_proxy": _fane_bass15_proxy,
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


def _directivity_db(f, theta_rad, dist, size_in, mic="sm57"):
    if abs(theta_rad) < 1e-4:
        return np.zeros_like(f)
    a = speaker_geom(size_in)[0]
    p = POSITION_PARAMS
    a_eff = a / (1.0 + (f / p["f_decouple"]) ** p["p_decouple"])
    ka = np.maximum(2.0 * np.pi * f / C_SOUND * a_eff * np.sin(abs(theta_rad)), 1e-9)
    db = 20.0 * np.log10(np.maximum(np.abs(2.0 * j1(ka) / ka), 1e-4))
    db = db * (dist / (dist + a))       # suavizado de campo cercano

    # El patrón polar pertenece al mic, no al parlante. Para los cardioides la
    # directividad de pistón ya reproduce el delta medido del SM57 (0.5 dB de
    # error medio). El R121 figura-8 necesita el residual medido propio.
    pattern = MICS[mic][2]
    if pattern == "fig8":
        amount = min(abs(theta_rad) / np.deg2rad(45.0), 1.5)
        db += amount * _log_interp_db(
            f, _MIC_CAL_FREQS, _R121_ANGLE_45_DB)
    return db


def _proximity_db(f, dist, mic):
    _, strength, pattern = MICS[mic]
    if pattern == "omni" or strength <= 0.0:
        return np.zeros_like(f)
    d_eff = np.hypot(max(dist, 0.005), 0.08)   # campo cercano fuente grande
    gain = strength * (0.025 / d_eff)
    return gain / (1.0 + np.exp(np.log2(np.maximum(f, 1.0) / 250.0) * 2.0))


def _mic_response_db(f, dist, mic):
    curve, _, _ = MICS[mic]
    return curve(f) + _mic_calibration_db(f, mic) + _proximity_db(f, dist, mic)


# Cuánto se exagera el CARÁCTER de cada mic respecto al SM57 (que queda
# intacto por construcción). Con las curvas calibradas 1:1 los no-dinámicos
# difieren sobre todo en 8-10k — banda que el cliff del parlante (~6.5k)
# entierra — y en el juego sonaban "completamente iguales" (validado por el
# usuario A/B). 1.0 = físico puro; 1.8 = separación claramente audible
# partiendo de las firmas REALES (medidas + residuales), no de voicings
# inventados. Se aplica en synthesize_ir sobre _mic_response_db completo.
MIC_CHARACTER = 1.8

# Tope del delta estirado (dB). Sin tope, el R121 del seed de Muse
# (ribbon@cone x carácter 1.8 sobre un 4x12 cerrado con bloom) apilaba
# +13 dB de graves y -8 dB en 6 kHz: "el disco es oscuro pero no tanto,
# y se pierde en el mix al tocar". El clip conserva la identidad (orden
# y forma de los deltas) pero garantiza que TODO mic siga usable en
# mezcla: graves acotados (sin barro) y presencia nunca enterrada.
_MIC_CHARACTER_MAX_DB = 9.0
_MIC_CHARACTER_MIN_DB = -6.0


def _mic_character_db(f, dist, mic):
    """Respuesta de mic con el carácter estirado alrededor del SM57."""
    ref = _mic_response_db(f, dist, "sm57")
    if mic == "sm57" or MIC_CHARACTER == 1.0:
        return _mic_response_db(f, dist, mic)
    delta = MIC_CHARACTER * (_mic_response_db(f, dist, mic) - ref)
    return ref + np.clip(delta, _MIC_CHARACTER_MIN_DB, _MIC_CHARACTER_MAX_DB)


def _enclosure_db(f, drivers=4, size_in=12.0, back="closed", baffle_m=0.76,
                  width_m=None, height_m=None, depth_m=None, volume_l=None,
                  panel_thickness_m=None, tuning_hz=None, dipole_hz=None,
                  open_back_ratio=None, low_cut_hz=None,
                  cab_resonance_hz=None):
    """First-order enclosure acoustics from documented cabinet geometry.

    Nominal impedance, material and construction notes remain catalog metadata:
    a static IR cannot reproduce amp/load interaction, and panel vibration needs
    measured mechanical data. Dimensions, volume and port/dipole tuning do have
    deterministic acoustic effects and are consumed here.
    """
    d = np.zeros_like(f)
    width = float(width_m) if width_m else float(baffle_m)
    height = float(height_m) if height_m else None
    depth = float(depth_m) if depth_m else None
    f_baffle = C_SOUND / (np.pi * max(width, 0.2))
    x = np.log2(np.maximum(f, 1.0) / f_baffle)
    d += 6.0 / (1.0 + np.exp(-x * 1.8)) - 6.0

    # If the manufacturer omits internal volume, estimate it conservatively
    # from the external dimensions. This is intentionally disabled for open
    # backs, where the dipole path dominates over sealed-cavity compliance.
    volume = float(volume_l) if volume_l else None
    if volume is None and back != "open" and height and depth:
        wall = float(panel_thickness_m or 0.019)
        iw = max(width - 2.0 * wall, 0.1)
        ih = max(height - 2.0 * wall, 0.1)
        id_ = max(depth - 2.0 * wall, 0.1)
        volume = 1000.0 * iw * ih * id_ * 0.88  # drivers/bracing displacement

    # A cabinet's first axial modes are low-amplitude but audible broad color,
    # especially around 200-650 Hz. Keep them subtle because damping/bracing
    # are not documented in most manuals.
    for dimension, gain in ((width, 0.55), (height, 0.45), (depth, -0.45)):
        if dimension:
            d += _bump(f, C_SOUND / (2.0 * dimension), gain, 0.42)

    ref_volume_per_driver = 40.0 * (float(size_in) / 12.0) ** 3
    volume_per_driver = volume / max(drivers, 1) if volume else None
    if back == "closed":
        # bloom medido en el 4x12 (+11 dB <550); escala ±3 dB por duplicación
        bloom = 11.0 + 3.0 * np.log2(max(drivers, 1) / 4.0)
        d += bloom / (1.0 + np.exp(np.log2(np.maximum(f, 1.0) / 550.0) * 1.6))
        resonance = float(cab_resonance_hz) if cab_resonance_hz else None
        if resonance is None and volume_per_driver:
            resonance = 105.0 * np.sqrt(
                ref_volume_per_driver / max(volume_per_driver, 5.0))
        if resonance:
            d += _bump(f, np.clip(resonance, 45.0, 180.0), 1.6, 0.46)
    elif back == "ported":
        # Bass reflex: documented tuning wins; otherwise estimate it from the
        # volume per driver while retaining 55 Hz as the legacy reference.
        fb = float(tuning_hz) if tuning_hz else None
        if fb is None and volume_per_driver:
            fb = 55.0 * np.sqrt(
                ref_volume_per_driver / max(volume_per_driver, 5.0))
        fb = float(np.clip(fb or 55.0, 28.0, 110.0))
        bloom = 10.0 + 3.0 * np.log2(max(drivers, 1) / 4.0)
        d += bloom / (1.0 + np.exp(np.log2(np.maximum(f, 1.0) / 500.0) * 1.6))
        d += _bump(f, fb, 3.5, 0.5)
        unload_hz = min(float(low_cut_hz or (0.69 * fb)), 0.82 * fb)
        d -= 10.0 / (
            1.0 + np.exp(np.log2(np.maximum(f, 1.0) / unload_hz) * 3.0))
    else:
        # Open back: rear radiation cancels bass according to the shortest path
        # around the baffle. An explicit calibrated value can override geometry.
        if dipole_hz:
            dipole = float(dipole_hz)
        elif height and depth:
            path = 0.5 * min(width, height) + 0.5 * depth
            dipole = C_SOUND / (2.0 * np.pi * max(path, 0.15))
        else:
            dipole = 140.0
        opening = float(np.clip(
            1.0 if open_back_ratio is None else open_back_ratio, 0.15, 1.0))
        cancellation = 8.0 * (0.72 + 0.28 * opening)
        bloom = 8.0 + 3.0 * np.log2(max(drivers, 1) / 2.0)
        d += bloom / (1.0 + np.exp(np.log2(np.maximum(f, 1.0) / 450.0) * 1.6))
        d -= cancellation / (
            1.0 + np.exp(np.log2(np.maximum(f, 1.0) / dipole) * 2.0))
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


def _with_hf_driver(lf_db, f, kind=None, crossover_hz=None,
                    level_db=None, rolloff_hz=None):
    """Power-sum a distant horn/tweeter into the close-miked LF radiator.

    Catalog HF levels include the real attenuator setting plus spatial loss:
    Cab Room positions sit on a woofer, not directly on the horn. This keeps
    the extension audible without turning a close-miked bass IR into a PA IR.
    """
    if not kind:
        return lf_db
    fc = float(crossover_hz or (4200.0 if kind == "piezo" else 3500.0))
    top = float(rolloff_hz or 17000.0)
    level = float(-14.0 if level_db is None else level_db)
    order = 1 if kind == "piezo" else 2
    hf_db = level + _hp_db(f, fc, order) + _lp_db(f, top, 2)
    if kind == "piezo":
        hf_db += _bump(f, 6200.0, 1.8, 0.45)
        hf_db += _bump(f, 10500.0, -2.2, 0.5)
    elif kind == "titanium":
        hf_db += _bump(f, 5200.0, 1.0, 0.55)
        hf_db += _bump(f, 11000.0, 0.7, 0.65)
    elif kind == "mesa":
        hf_db += _bump(f, 6500.0, 0.6, 0.7)

    lf_power = 10.0 ** (lf_db / 10.0)
    hf_power = 10.0 ** (hf_db / 10.0)
    return 10.0 * np.log10(np.maximum(lf_power + hf_power, 1e-12))


def _calibrate_system_lf(db, f, f3_hz=None, f6_hz=None, f10_hz=None):
    """Constrain a proxy to manufacturer-published low-frequency anchors."""
    if not any(value is not None for value in (f3_hz, f6_hz, f10_hz)):
        return db
    known = [float(v) for v in (f3_hz, f6_hz, f10_hz) if v is not None]
    ref_hz = max(160.0, min(250.0, max(known) * 3.0))
    ref_db = float(np.interp(ref_hz, f, db))
    anchors = []
    for hz, target in ((f10_hz, -10.0), (f6_hz, -6.0), (f3_hz, -3.0)):
        if hz is None:
            continue
        hz = float(hz)
        current_rel = float(np.interp(hz, f, db)) - ref_db
        anchors.append((hz, target - current_rel))
    anchors.append((ref_hz, 0.0))
    anchors.sort()
    af = np.array([item[0] for item in anchors], dtype=float)
    ac = np.array([item[1] for item in anchors], dtype=float)
    correction = np.interp(
        np.log(np.maximum(f, 1.0)), np.log(af), ac,
        left=float(ac[0]), right=0.0)
    return db + correction


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
                  baffle_m=0.76, voicing=None, width_m=None, height_m=None,
                  depth_m=None, volume_l=None, panel_thickness_m=None,
                  tuning_hz=None, dipole_hz=None, open_back_ratio=None,
                  low_cut_hz=None, cab_resonance_hz=None, hf_kind=None,
                  hf_crossover_hz=None, hf_level_db=None,
                  hf_rolloff_hz=None, system_f3_hz=None, system_f6_hz=None,
                  system_f10_hz=None, sr=48000, n_fft=8192, n_out=2048):
    """Parámetros físicos → IR float32 fase-mínima listo para el engine.

    Si `voicing` está en VOICINGS (cabs novelty), se usa esa curva de
    carácter en vez del driver+caja. Posición, ángulo y mic siguen actuando a
    escala reducida para que los controles del Cab Room no sean decorativos."""
    f = np.fft.rfftfreq(n_fft, 1.0 / sr)
    f[0] = f[1]
    dist = float(dist_in) * 0.0254 + 0.015    # + offset de rejilla
    if voicing and voicing in VOICINGS:
        db = VOICINGS[voicing](f)
        db += 0.35 * _position_db(f, x, dist, size_in)
        db += 0.35 * _directivity_db(
            f, np.deg2rad(angle_deg), dist, size_in, mic)
        db += 0.35 * _mic_character_db(f, dist, mic)
    else:
        db = DRIVERS[speaker](f)
        db = db + _position_db(f, x, dist, size_in)
        db = db + _directivity_db(
            f, np.deg2rad(angle_deg), dist, size_in, mic)
        db = _with_hf_driver(
            db, f, kind=hf_kind, crossover_hz=hf_crossover_hz,
            level_db=hf_level_db, rolloff_hz=hf_rolloff_hz)
        db = db + _mic_character_db(f, dist, mic)
        db = db + _enclosure_db(
            f, drivers, size_in, back, baffle_m,
            width_m=width_m, height_m=height_m, depth_m=depth_m,
            volume_l=volume_l, panel_thickness_m=panel_thickness_m,
            tuning_hz=tuning_hz, dipole_hz=dipole_hz,
            open_back_ratio=open_back_ratio, low_cut_hz=low_cut_hz,
            cab_resonance_hz=cab_resonance_hz)
        db = _calibrate_system_lf(
            db, f, f3_hz=system_f3_hz, f6_hz=system_f6_hz,
            f10_hz=system_f10_hz)
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
