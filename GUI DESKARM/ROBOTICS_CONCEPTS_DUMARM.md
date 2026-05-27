# 🤖 Concetti di Robotica — Analisi del Progetto DUMARM

> Questo documento mappa i principali concetti teorici di robotica alla loro implementazione **concreta** nel codice del progetto DUMARM (GUI Python + Firmware MCU).

---

## Il Robot: DUMARM

Il DUMARM è un **manipolatore planare seriale a 2 DOF** (Gradi di Libertà):
- **Giunto 1** (spalla): $q_0$ — motore con encoder
- **Giunto 2** (gomito): $q_1$ — motore con encoder
- **Organo terminale**: penna (up/down controllato da servo)
- **Dimensioni fisiche**: $l_1 = 0.128$ m, $l_2 = 0.144$ m (da `config.py`)

```
          ●── l2 ──▶ [Penna]
         /
    ── l1 ──
   /
[Base]
```

---

## 1. Pose di Corpo Rigido e Matrici di Rotazione

### Teoria
Un corpo rigido nello spazio 3D è descritto da una matrice omogenea $T \in SE(3)$ (4×4).
Per robot planari 2D, la matrice si riduce a $T \in SE(2)$:

$$T = \begin{bmatrix} \cos\theta & -\sin\theta & x \\ \sin\theta & \cos\theta & y \\ 0 & 0 & 1 \end{bmatrix}$$

### Nel DUMARM

Siccome il robot è **planare**, non servono matrici $SO(3)$ 3D complete.
La posa dell'end-effector è descritta semplicemente dal vettore $(x, y, \theta)$:

$$\theta = q_0 + q_1 \quad \text{(angolo totale dell'end-effector)}$$

Questo è calcolato dalla **cinematica diretta** `dk()` in `lib/trajpy.py`:

```python
# lib/trajpy.py — riga 490
def dk(q: np.ndarray, sizes={'l1': 0.128, 'l2': 0.144}) -> np.ndarray:
    x     = l1*cos(q0) + l2*cos(q0 + q1)   # posizione X
    y     = l1*sin(q0) + l2*sin(q0 + q1)   # posizione Y
    theta = q0 + q1                          # orientamento
    return np.array([[x, y, theta]]).T
```

> **Nota:** La classe `Point` (riga 507 di `trajpy.py`) implementa operazioni vettoriali 2D (addizione, sottrazione, rotazione, prodotto scalare, angolo) che equivalgono alle operazioni di un vettore di traslazione 2D.

```python
def rotate(self, phi):
    angle  = self.angle() + phi            # ruota di phi
    length = self.mag()                    # norma invariante
    return Point(length*cos(angle), length*sin(angle))
```

---

## 2. Cinematica Diretta e Inversa (FK / IK)

### Cinematica Diretta — `dk(q)`

$$\begin{cases}
x = l_1 \cos(q_0) + l_2 \cos(q_0 + q_1) \\
y = l_1 \sin(q_0) + l_2 \sin(q_0 + q_1) \\
\theta = q_0 + q_1
\end{cases}$$

Usata in `read_position_cartesian()` (`gui_interface.py`) per visualizzare la posizione attuale in coordinate cartesiane a partire dalla lettura degli encoder.

### Cinematica Inversa — `ik(x, y)`

Data la posizione desiderata $(x, y)$, trovare $q_0, q_1$. Soluzione analitica geometrica:

$$q_1 = \pm \arccos\!\left(\frac{x^2 + y^2 - l_1^2 - l_2^2}{2 l_1 l_2}\right)$$

$$q_0 = \text{atan2}(y, x) - \text{atan2}(l_2 \sin q_1,\; l_1 + l_2 \cos q_1)$$

Il **doppio segno ±** genera **due soluzioni** (configurazione "elbow-up" e "elbow-down"). Il codice seleziona automaticamente la soluzione più vicina alla posizione attuale del robot usando la distanza minima nello spazio dei giunti (**seed continuity**):

```python
# lib/trajpy.py — selezione soluzione ottimale (riga 458)
for (q1, q2) in valid_solutions:
    dist = (q1 - curr_q1)**2 + (q2 - curr_q2)**2
    if dist < min_dist:
        best_sol = (q1, q2)
```

Questo garantisce la **continuità della traiettoria** evitando salti improvvisi tra configurazioni.

---

## 3. Cinematica Differenziale (Jacobiano)

### Teoria

$$\dot{x} = J(q)\,\dot{q} \quad \Rightarrow \quad \begin{bmatrix}\dot{x}\\\dot{y}\end{bmatrix} = \begin{bmatrix} -l_1\sin q_0 - l_2\sin(q_0+q_1) & -l_2\sin(q_0+q_1) \\ l_1\cos q_0 + l_2\cos(q_0+q_1) & l_2\cos(q_0+q_1) \end{bmatrix} \begin{bmatrix}\dot{q}_0\\\dot{q}_1\end{bmatrix}$$

### Nel DUMARM

Lo Jacobiano **non è calcolato esplicitamente** come matrice. Invece, la conversione da velocità cartesiane a velocità dei giunti avviene **implicitamente** attraverso:

1. La legge oraria $s(t) \in [0,1]$ parametrizza gli spostamenti cartesiani lungo ogni patch
2. La IK viene applicata punto per punto a ogni time-step $T_c$
3. Le velocità $\dot{q}$ emergono per differenza finita tra posizioni successive

Questo equivale a usare lo pseudo-inverso Jacobiano ma evitando singolarità computazionalmente.

---

## 4. Motion Planning vs. Trajectory Planning ✅✅

Questi due concetti sono il **cuore del progetto**, ben separati e implementati.

### 4.1 Motion Planning (geometria pura)

> **"Da dove passo?"** — Costruisce il percorso geometrico nell'operational space.

**File:** `gui_interface.py` → `merge_to_polylines()`

Il pianificatore riceve le **patches** (primitive geometriche) dal frontend JavaScript (canvas HTML) e le unifica in una sequenza di waypoints cartesiani:

| Tipo Patch | Descrizione |
|---|---|
| `line` | Segmento rettilineo da $(x_0,y_0)$ a $(x_1,y_1)$ |
| `circle` | Arco di cerchio con centro, raggio e angolo |
| `polyline` | Sequenza di punti (per testo/immagini elaborate) |

Dopo aver unificato le patches, applica l'**IK** per ogni waypoint convertendo il percorso da operational space a joint space.

Ottimizzazione: `py_optimize_trajectory()` semplifica la polilinea eliminando punti ridondanti entro una tolleranza di 1 mm.

### 4.2 Trajectory Planning (legge oraria)

> **"A che velocità ci passo?"** — Aggiunge la dimensione temporale al percorso.

**File:** `lib/trajpy.py` → `slice_trj()` + `get_profile_law()`

Il cuore è la funzione $s(t) \in [0,1]$ (legge oraria normalizzata) che mappa il tempo nella posizione lungo il percorso. Per ogni patch, il sistema calcola automaticamente la durata $t_f$ rispettando i vincoli:

- $|\ddot{q}| \leq \dot{q}_{max}^{acc}$ = `max_acc` (da `config.py`: 0.1 rad/s²)
- $|\dot{q}| \leq \dot{q}_{max}^{vel}$ = `max_speed` (da `config.py`: 10.0 rad/s)

#### Profili disponibili e loro caratteristiche

| Profilo | $s(t)$ | $v_{peak}$ | $a_{peak}$ | Jerk | Uso |
|---|---|---|---|---|---|
| **Trapezoidale** | Parabolico + Lineare + Parabolico | $v_{max}$ | $a_{max}$ costante | Discontinuo | Default, veloce |
| **S-Curve / Cicloidale** | $\frac{t}{t_f} - \frac{\sin(2\pi t/t_f)}{2\pi}$ | $\frac{2S}{t_f}$ | $\frac{2\pi S}{t_f^2}$ | Continuo | Morbido |
| **Cubico** (`polynomial3`) | $3\tau^2 - 2\tau^3$ | $1.5 \frac{S}{t_f}$ | $6\frac{S}{t_f^2}$ | Discontinuo | Preciso |
| **Quartico** (`polynomial4`) | $4\tau^3 - 3\tau^4$ | $1.778 \frac{S}{t_f}$ | $12\frac{S}{t_f^2}$ | Parziale | Bilanciato |
| **Quintico** (`polynomial5`) | $10\tau^3 - 15\tau^4 + 6\tau^5$ | $1.875 \frac{S}{t_f}$ | $5.77\frac{S}{t_f^2}$ | Continuo | Più morbido |

dove $\tau = t/t_f$ e $S$ è la distanza totale da percorrere.

**Selezionabile dall'utente** via `py_set_motion_profile()` in runtime senza riavviare.

#### Parametro $T_c$ — Il passo temporale

Il parametro $T_c$ (da `config.py`: default 10 ms) è il **passo temporale di campionamento** della traiettoria:
- Ogni $T_c$ secondi viene inviato un setpoint $(q, \dot{q}, \ddot{q})$ al firmware
- Frequenza di controllo: $f_c = 1/T_c = 100$ Hz (default)
- Modificabile in runtime con `py_set_tc()`

#### Flusso completo Motion + Trajectory Planning

```
[Canvas JS]
    ↓ patches (linee, archi, testo, immagini)
merge_to_polylines()           ← MOTION PLANNING (geometria)
    ↓ lista di patch {type, points, data}
slice_trj(patch, ...)          ← TRAJECTORY PLANNING (legge oraria)
    ↓ calcola get_profile_law(profile, distance, max_acc, max_speed)
    ↓ s(t): timing law normalizzata [0→1]
    ↓ per ogni t in 0..tf con passo Tc:
    ↓     punto_car = interpola(sp, ep, s(t))  ← punto cartesiano
    ↓     q = ik(x, y)                         ← joint space
    ↓ → (q0[], q1[], penup[], ts[])
validate_trajectory(q, dq, ddq)  ← sicurezza
TrajectoryExecutor.execute()     ← ESECUZIONE
```

---

## 5. Architettura di Controllo e Motion Control

### Architettura distribuita PC ↔ MCU

```
┌─────────────────────────────────────────┐
│              PC (Python/GUI)            │
│  Planning + Scheduling + Supervisore    │
│                                         │
│  TrajectoryExecutor                     │
│  ┌──────────────────────────────────┐   │
│  │  Loop drift-compensating @ Tc   │   │
│  │  Batch di BATCH_SIZE=5 punti    │   │
│  │  Pre-roll buffer firmware=50pt  │   │
│  └──────────────────────────────────┘   │
└────────────────┬────────────────────────┘
                 │ UART Seriale (CRC32)
                 │ Pacchetto: [A5][5A][CMD][q0][q1][dq0][dq1][ddq0][ddq1][pen][CRC]
                 ↓
┌─────────────────────────────────────────┐
│              MCU (Firmware C)           │
│  Controllo Real-Time                    │
│                                         │
│  PID Indipendente Giunto 1 (q0)        │
│  PID Indipendente Giunto 2 (q1)        │
│  Buffer FIFO 50 punti                  │
│  Lettura Encoder → feedback q attuale  │
│  Homing con limit switch               │
└─────────────────────────────────────────┘
```

### Protocollo di Comunicazione Binario (`lib/binary_protocol.py`)

Ogni pacchetto è strutturato come segue:

| Campo | Bytes | Valore/Tipo |
|---|---|---|
| Header 1 | 1 | `0xA5` (sync byte) |
| Header 2 | 1 | `0x5A` (sync byte) |
| CMD | 1 | `0x01`=Traiettoria, `0x02`=Homing, `0x03`=Stop |
| $q_0$ | 4 | `float32` little-endian |
| $q_1$ | 4 | `float32` little-endian |
| $\dot{q}_0$ | 4 | `float32` little-endian |
| $\dot{q}_1$ | 4 | `float32` little-endian |
| $\ddot{q}_0$ | 4 | `float32` little-endian |
| $\ddot{q}_1$ | 4 | `float32` little-endian |
| `pen_up` | 1 | `uint8` (0 o 1) |
| CRC32 | 4 | Verifica integrità |

**Totale:** 28 byte per punto di traiettoria.

Il firmware risponde con **feedback di posizione** (`RESP_POS`): $(q_0^{actual}, q_1^{actual})$ letti dagli encoder, usato per aggiornare la UI in closed-loop.

### Controllo in Feedback (Closed-Loop Lato GUI)

Lo `state.py` mantiene il **Global State** del robot in modo thread-safe:

```python
class FirmwareState:
    q0: float          # posizione giunto 1 (da encoder)
    q1: float          # posizione giunto 2 (da encoder)
    buffer_level: int  # livello buffer firmware (per flow control)

class RobotState:
    firmware: FirmwareState    # feedback dall'MCU (via seriale)
    last_trajectory: Dict      # ultima traiettoria generata
    log_data: Dict             # dati registrati per plotting
    stop_requested: bool       # flag di interruzione emergenza
```

Il thread seriale aggiorna `state.firmware.q0/q1` in real-time via `update_position()` (con lock) ogni volta che arriva un pacchetto feedback.

---

## 6. Dinamica (Eulero-Lagrange)

### Teoria (applicata al DUMARM 2R planare)

L'equazione del moto è:

$$M(q)\ddot{q} + C(q,\dot{q})\dot{q} + g(q) = \tau$$

Per un robot planare senza gravità significativa (piano orizzontale), $g(q) \approx 0$:

$$M(q) = \begin{bmatrix} I_1 + I_2 + m_2 l_1^2 + 2m_2 l_1 l_{c2}\cos q_1 & I_2 + m_2 l_1 l_{c2}\cos q_1 \\ I_2 + m_2 l_1 l_{c2}\cos q_1 & I_2 \end{bmatrix}$$

### Nel DUMARM

La dinamica **non è calcolata esplicitamente nel software Python** — è affidata al firmware MCU che implementa:

- **PID decentralizzato** per ogni giunto (controllo semplificato)
- Il firmware usa $\dot{q}_{ref}$ e $\ddot{q}_{ref}$ come **feedforward** per migliorare l'inseguimento
- Non c'è compensazione esplicita delle forze centrifughe $C(q,\dot{q})\dot{q}$

> **Perché funziona comunque bene?** Il DUMARM opera a basse velocità di scrittura e i link sono relativamente leggeri → i termini dinamici incrociati sono piccoli rispetto al controllo PID.

---

## 7. Validazione della Traiettoria

**File:** `gui_interface.py` → `validate_trajectory(q, dq, ddq)`

Prima di eseguire, il sistema verifica che la traiettoria rispetti i limiti fisici dei motori:

```python
# Calcola accelerazione massima effettiva
max_ddq_actual = max(max(abs(ddq[0])), max(abs(ddq[1])))

# Tolleranza: fino a 15x il limite in config (per gestire picchi di interpolazione)
MAX_ACC_TOLERANCE_FACTOR = 15.0

if max_ddq_actual > SETTINGS['max_acc'] * MAX_ACC_TOLERANCE_FACTOR:
    # Scala automaticamente la traiettoria (la rallenta)
    scale_factor = max_ddq_actual / (SETTINGS['max_acc'] * MAX_ACC_TOLERANCE_FACTOR)
```

La funzione restituisce un `scale_factor` che viene applicato rallentando proporzionalmente la traiettoria se necessario.

---

## 8. Homing (Calibrazione Zero)

Il sistema di homing porta il robot in **posizione nota** all'avvio usando i **limit switch** fisici:

**File:** `gui_interface.py` → `py_homing_cmd()`

```
Sequenza Homing:
1. PC invia CMD_HOMING via seriale
2. Firmware muove q0 lentamente → finché non aziona limit switch 1
3. Firmware muove q1 lentamente → finché non aziona limit switch 2
4. Firmware setta q0=0, q1=0 (zero angolare hardware)
5. Firmware invia ACK
6. PC muove il robot a posizione home software
```

Il firmware implementa anche **software endstops** per sicurezza aggiuntiva.

---

## 9. Pipeline Completa: dall'Immagine al Robot

Il caso più sofisticato del sistema è la modalità **IMAGE**:

```
[Immagine PNG/JPG]
      ↓
lib/image_processor.py
  → Conversione Grayscale
  → Sogliatura adattiva (Otsu)
  → Rilevamento contorni (Canny / edge detection)
  → Thinning / Skeletonization
  → Estrazione polilinee ordinate (TSP-like path planning)
  → Punti cartesiani (x, y) nel workspace robot
      ↓
merge_to_polylines()           [MOTION PLANNING]
      ↓
slice_trj() + get_profile_law() [TRAJECTORY PLANNING]
      ↓
validate_trajectory()          [SAFETY CHECK]
      ↓
TrajectoryExecutor.execute()   [ESECUZIONE via UART]
      ↓
MCC Firmware (PID) → Motori   [MOTION CONTROL]
      ↓
    🖊️ Disegno fisico
```

---

## 10. Plotting e Analisi Post-Esecuzione

**File:** `plotting.py`

Dopo ogni esecuzione, vengono generati grafici sincronizzati con:

| Subplot | Dati |
|---|---|
| $q_0(t)$ | Posizione giunto 1 (riferimento vs. effettivo) |
| $q_1(t)$ | Posizione giunto 2 (riferimento vs. effettivo) |
| $\dot{q}_0(t), \dot{q}_1(t)$ | Velocità dei giunti |
| $\ddot{q}_0(t), \ddot{q}_1(t)$ | Accelerazioni dei giunti |
| Percorso XY | Traiettoria cartesiana 2D |

Questi grafici permettono di verificare visivamente la qualità dell'inseguimento della traiettoria.

---

## 11. Force Control e Visual Servoing

### Force Control

**Non implementato** — il DUMARM scrive su superfici piane con una penna.
Il contatto è gestito solo con il flag binario `pen_up/pen_down`.

Se si volesse aggiungere Force Control:
- Servirebbe un sensore di forza/coppia sull'end-effector
- Si implementerebbe impedenza: $F = K(x_{ref} - x) + D(\dot{x}_{ref} - \dot{x})$

### Visual Servoing

**Non implementato** come closed-loop in real-time.
`lib/image_processor.py` fa solo image processing **offline** (pre-processing una tantum).

---

## 📊 Tabella Riassuntiva Finale

| Concetto | Presenza | File/Funzione | Note |
|---|:---:|---|---|
| **FK** (Cinematica Diretta) | ✅ | `trajpy.py` → `dk()` | Formula 2R planare |
| **IK** (Cinematica Inversa) | ✅ | `trajpy.py` → `ik()` | 2 soluzioni + seed selection |
| Matrici $SO(3)/SE(3)$ | ⚠️ | N/A | Non necessarie (robot 2D) |
| Jacobiano $J(q)$ | ⚠️ | Implicito in `ik()` | Nessun calcolo esplicito |
| **Motion Planning** | ✅ | `gui_interface.py` → `merge_to_polylines()` | Geometria pura cartesiana |
| **Trajectory Planning** | ✅✅ | `trajpy.py` → `slice_trj()` | 5 profili selezionabili |
| Profilo Trapezoidale | ✅ | `get_profile_law()` | Default |
| Profilo S-Curve/Cicloidale | ✅ | `get_profile_law()` | Jerk-continuous |
| Profilo Cubico/Quartico/Quintico | ✅ | `polynomial3/4/5()` | Polinomiali normalizzati |
| Homing / Calibrazione | ✅ | `py_homing_cmd()` + firmware | Limit switch hardware |
| Protocollo Binario CRC32 | ✅ | `binary_protocol.py` | 28 byte/punto |
| Global State Thread-Safe | ✅ | `state.py` | Lock per feedback seriale |
| Validazione Traiettoria | ✅ | `validate_trajectory()` | Auto-scaling |
| Dinamica $M(q)\ddot{q}+...$ | ❌ | Solo nel firmware MCU | PID decentralizzato |
| Motion Control (PID) | ⚠️ | Firmware MCU (C) | Feedforward $\dot{q}, \ddot{q}$ |
| Force Control | ❌ | Non implementato | Non necessario |
| Visual Servoing | ❌ | Solo image processing offline | Non closed-loop |
| **Image-to-Robot Pipeline** | ✅ | `image_processor.py` | Completa |
| Plotting Analisi | ✅ | `plotting.py` | Riferimento vs. Effettivo |

---

*Generato il 01/03/2026 — Progetto DUMARM GUI*
*Dimensioni fisiche: $l_1=0.128$ m, $l_2=0.144$ m — Frequenza di controllo: $f_c = 100$ Hz ($T_c=10$ ms)*
