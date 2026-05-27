import { appState, TOOLS } from './state.js';
import { CanvasHandler } from './canvas.js';
import { Point, abs2rel } from './utils.js';
import { simplifyPath } from './utils_drawing.js';
import { TrajectoryOptimizer } from './trajectory_optimizer.js';
import { API } from './api.js';
import { ThemeManager } from './theme.js';

// --- Initialization ---

const canvas = document.getElementById('input_canvas');
const state = appState;

// Initialize State
state.init(canvas.width, canvas.height);

// Initialize Canvas Handler
const canvasHandler = new CanvasHandler(canvas, state);

// --- UI Elements ---

// --- UI Elements ---
// Initialized in initApp() to ensure DOM is ready
let ui = {};

function initUI() {
    ui = {
        statusDot: document.getElementById('status-dot'),
        statusText: document.getElementById('status-text'),
        btnConnect: document.getElementById('start-serial-btn'),

        btnModeContinuous: document.getElementById('mode-continuous-btn'),
        btnModeDiscrete: document.getElementById('mode-discrete-btn'),
        btnErase: document.getElementById('erase-mode-btn'),

        btnLine: document.getElementById('tool-line'),
        btnCircle: document.getElementById('tool-circle'),
        btnSquare: document.getElementById('tool-square'),
        btnPolygon: document.getElementById('tool-polygon'),
        btnSemicircle: document.getElementById('tool-semicircle'),
        btnStar: document.getElementById('tool-star'),
        btnHeart: document.getElementById('tool-heart'),
        btnSpiral: document.getElementById('tool-spiral'),
        btnCross: document.getElementById('tool-greek-cross'),
        btnRhombus: document.getElementById('tool-rhombus'),
        btnTriangle: document.getElementById('tool-triangle'),
        btnTriangleScalene: document.getElementById('tool-triangle-scalene'),
        btnTriangleRight: document.getElementById('tool-triangle-right'),
        btnTriangleIsosceles: document.getElementById('tool-triangle-isosceles'),

        btnUndo: document.getElementById('undo-btn'),
        btnRedo: document.getElementById('redo-btn'),
        btnClear: document.getElementById('clear-btn'),
        btnGridToggle: document.getElementById('grid-toggle-btn'),

        btnSend: document.getElementById('send-trajectory-btn'),
        btnStop: document.getElementById('stop-trajectory-btn'),
        btnHoming: document.getElementById('homing-btn'),
        btnCleanState: document.getElementById('clean-state-btn'),
        btnPlotToggle: document.getElementById('plot-toggle-btn'),

        // Text Tools
        btnPresetLetter: document.getElementById('btn-preset-letter'),
        btnModeLinear: document.getElementById('mode-linear-btn'),
        btnModeCurved: document.getElementById('mode-curved-btn'),
        inputText: document.getElementById('text-input'),
        inputFontSize: document.getElementById('font-size'),
        controlsLinear: document.getElementById('linear-controls'),
        controlsCurved: document.getElementById('curved-controls'),

        // Linear Inputs
        inputLinX: document.getElementById('lin-x'),
        inputLinY: document.getElementById('lin-y'),
        inputLinAngle: document.getElementById('lin-angle'),

        inputWsX: document.getElementById('ws-x'),
        inputWsY: document.getElementById('ws-y'),
        inputWsW: document.getElementById('ws-w'),
        inputWsH: document.getElementById('ws-h'),
        selectPaperSize: document.getElementById('ws-paper-size'),
        paperSizeBlock: document.getElementById('paper-size-block'),

        // Curved Inputs (Text positioning)
        inputCurvRadius: document.getElementById('curv-radius'),
        inputCurvOffset: document.getElementById('curv-offset'),

        // Curved Workspace Config
        inputWsInnerR: document.getElementById('ws-inner-r'),
        inputWsOuterR: document.getElementById('ws-outer-r'),

        warningMsg: document.getElementById('text-warning'),
        btnAddText: document.getElementById('btn-add-text'),
        // Redundant buttons removed
        // btnGenerate, btnNewline, btnClean removed

        // App Mode
        btnAppModeDrawing: document.getElementById('app-mode-drawing'),
        btnAppModeText: document.getElementById('app-mode-text'),
        sectionDrawing: document.getElementById('section-drawing-tools'),
        sectionText: document.getElementById('section-text-tools'),

        // Workspace Geometry Controls
        linearWsControls: document.getElementById('linear-ws-controls'),
        curvedWsControls: document.getElementById('curved-ws-controls'),

        // CAD Import Panel
        cadPanel: document.getElementById('cad-import-panel'),
        cadPanelHeader: document.getElementById('cad-panel-header'),
        cadFileInput: document.getElementById('cad-file-input'),
        btnSelectCad: document.getElementById('btn-select-cad'),
        cadFileName: document.getElementById('cad-file-name'),
        cadWarning: document.getElementById('cad-warning'),
        btnConvertCad: document.getElementById('btn-convert-cad'),
        cadControls: document.getElementById('cad-controls'),
        cadUnits: document.getElementById('cad-units'),
        cadScale: document.getElementById('cad-scale'),
        cadOffX: document.getElementById('cad-off-x'),
        cadOffY: document.getElementById('cad-off-y'),
        btnAutoFit: document.getElementById('btn-auto-fit'),
        btnConfirmImport: document.getElementById('btn-confirm-import'),
        btnCancelImport: document.getElementById('btn-cancel-import'),

        // Image Tools
        btnAppModeImage: document.getElementById('app-mode-image'),
        sectionImage: document.getElementById('section-image-tools'),
        inputImageFile: document.getElementById('image-upload'),
        imgPreviewContainer: document.getElementById('image-preview-container'),
        imgPreviewImg: document.getElementById('image-preview-img'),
        inputImgWidth: document.getElementById('img-width'),
        inputImgHeight: document.getElementById('img-height'),
        inputImgLockRatio: document.getElementById('img-lock-ratio'),
        inputImgX: document.getElementById('img-x'),
        inputImgY: document.getElementById('img-y'),
        inputImgRotation: document.getElementById('img-rotation'),
        inputImgThreshold: document.getElementById('img-threshold'),
        btnProcessImage: document.getElementById('btn-process-image'),
        btnConfirmImage: document.getElementById('btn-confirm-image'),

        // Motion Control
        selectMotionProfile: document.getElementById('motion-profile-select'),
        inputMotionAcc: document.getElementById('motion-acc-input'),
        inputMotionSpeed: document.getElementById('motion-speed-input'),
        inputMotionTc: document.getElementById('motion-tc'),
    };
}

// --- Event Listeners Wrapper ---

function setupEventListeners() {
    if (!ui.btnConnect) {
        console.error("GUI not initialized!");
        return;
    }

    // Connection
    ui.btnConnect.addEventListener('click', async () => {
        await API.startSerial();
        updateSerialStatus();
    });

    setupImageEvents();

    // Drawing Modes
    ui.btnModeContinuous.addEventListener('click', () => {
        state.drawingMode = 'continuous';
        ui.btnModeContinuous.classList.add('active');
        ui.btnModeDiscrete.classList.remove('active');

        // Disable erase mode if active
        state.eraseMode = false;
        ui.btnErase.classList.remove('active');

        setTool(state.tool); // Reset partial states
    });

    ui.btnModeDiscrete.addEventListener('click', () => {
        state.drawingMode = 'discrete';
        ui.btnModeDiscrete.classList.add('active');
        ui.btnModeContinuous.classList.remove('active');

        // Disable erase mode if active
        state.eraseMode = false;
        ui.btnErase.classList.remove('active');

        setTool(state.tool); // Reset partial states
    });

    // Erase Mode
    if (ui.btnErase) {
        ui.btnErase.addEventListener('click', () => {
            state.eraseMode = !state.eraseMode;
            ui.btnErase.classList.toggle('active', state.eraseMode);

            // Optional: visual feedback or cursor change
        });
    }

    // Tools
    ui.btnLine.addEventListener('click', () => {
        setTool(TOOLS.LINE);
        updateToolUI();
    });

    ui.btnCircle.addEventListener('click', () => {
        setTool(TOOLS.CIRCLE);
        updateToolUI();
    });

    ui.btnSquare.addEventListener('click', () => {
        setTool(TOOLS.SQUARE);
        updateToolUI();
    });

    ui.btnPolygon.addEventListener('click', () => {
        setTool(TOOLS.POLYGON);
        updateToolUI();

        const sides = prompt('Number of sides (3-12):', '5');
        if (sides && !isNaN(sides)) {
            state.polygonSides = parseInt(sides);
            if (state.polygonSides < 3) state.polygonSides = 3;
            if (state.polygonSides > 12) state.polygonSides = 12;
        }
    });

    ui.btnSemicircle.addEventListener('click', () => {
        setTool(TOOLS.SEMICIRCLE);
        updateToolUI();
    });

    ui.btnStar.addEventListener('click', () => {
        setTool(TOOLS.STAR);
        updateToolUI();
    });

    if (ui.btnHeart) {
        ui.btnHeart.addEventListener('click', () => {
            setTool(TOOLS.HEART);
            updateToolUI();
        });
    }

    if (ui.btnSpiral) {
        ui.btnSpiral.addEventListener('click', () => {
            setTool(TOOLS.SPIRAL);
            updateToolUI();
        });
    }

    if (ui.btnCross) {
        ui.btnCross.addEventListener('click', () => {
            setTool(TOOLS.CROSS);
            updateToolUI();
        });
    }

    if (ui.btnRhombus) {
        ui.btnRhombus.addEventListener('click', () => {
            setTool(TOOLS.RHOMBUS);
            updateToolUI();
        });
    }

    if (ui.btnTriangle) {
        ui.btnTriangle.addEventListener('click', () => {
            setTool(TOOLS.TRIANGLE);
            updateToolUI();
        });
    }

    if (ui.btnTriangleScalene) {
        ui.btnTriangleScalene.addEventListener('click', () => {
            setTool(TOOLS.TRIANGLE_SCALENE);
            updateToolUI();
        });
    }

    if (ui.btnTriangleRight) {
        ui.btnTriangleRight.addEventListener('click', () => {
            setTool(TOOLS.TRIANGLE_RIGHT);
            updateToolUI();
        });
    }

    if (ui.btnTriangleIsosceles) {
        ui.btnTriangleIsosceles.addEventListener('click', () => {
            setTool(TOOLS.TRIANGLE_ISOSCELES);
            updateToolUI();
        });
    }

    // Undo/Redo/Clear - Unified
    ui.btnUndo.addEventListener('click', () => {
        state.undo();
        updateUndoRedoUI();
    });

    ui.btnRedo.addEventListener('click', () => {
        state.redo();
        updateUndoRedoUI();
    });

    ui.btnClear.addEventListener('click', () => {
        if (confirm('Are you sure to clear your workspace?')) {
            state.resetWorkspace(); // Clears all and saves state
            // State observer will handle UI updates
        }
    });

    // Grid Controls
    ui.btnGridToggle.addEventListener('click', () => {
        state.showGrid = !state.showGrid;
        ui.btnGridToggle.textContent = state.showGrid ? '⊞ Grid: ON' : '⊞ Grid: OFF';
        ui.btnGridToggle.classList.toggle('active', state.showGrid);
    });



    // Commands
    ui.btnHoming.addEventListener('click', () => {
        API.homing();
    });

    ui.btnSend.addEventListener('click', async () => {
        await API.sendData();
        // If PLOT toggle is ON, save trajectory analysis after generating
        if (ui.btnPlotToggle && ui.btnPlotToggle.classList.contains('active')) {
            const modeName = state.appMode || 'unknown';
            console.log(`[PLOT] Saving plots for mode: ${modeName}...`);
            const ok = await API.savePlots(modeName);
            if (ok) console.log('[PLOT] Trajectory analysis saved to images/');
            else console.warn('[PLOT] Plot saving failed or no data available.');
        }
    });

    ui.btnCleanState.addEventListener('click', () => {
        console.log("Cleaning all states..");
        // Reset drawing state
        state.points = [];
        state.sentPoints = [];
        state.trajectory.reset();
        state.sentTrajectory.reset();
        state.shapeStart = null;
        state.semicircleStart = null;
        state.circleDefinition = [];

        // Reset text state
        state.text = '';
        state.textPreview = [];
        state.generatedTextPatches = [];
        if (ui.inputText) ui.inputText.value = '';

        // Reset manipulator traces
        if (state.manipulator) state.manipulator.reset_trace();

        // Clear history for clean slate
        state.history = [];
        state.historyIndex = -1;
        state.saveState(); // Save the clean state

        // Update UI
        updateUndoRedoUI();
        if (canvasHandler) canvasHandler.animate();
        console.log("State cleaned.");
    });



    // Stop Trajectory
    ui.btnStop.addEventListener('click', async () => {
        console.log("Stopping trajectory...");
        await API.stopTrajectory();
    });

    // Plot Toggle
    if (ui.btnPlotToggle) {
        ui.btnPlotToggle.addEventListener('click', () => {
            const isOn = ui.btnPlotToggle.classList.toggle('active');
            ui.btnPlotToggle.textContent = isOn ? '📊 PLOT: ON' : '📊 PLOT: OFF';
        });
    }

    // --- Mode Selection ---
    if (ui.btnAppModeDrawing && ui.btnAppModeText) {
        ui.btnAppModeDrawing.addEventListener('click', () => setAppMode('drawing'));
        ui.btnAppModeText.addEventListener('click', () => setAppMode('text'));

        if (ui.btnAppModeImage) {
            ui.btnAppModeImage.addEventListener('click', () => setAppMode('image'));
        }

        setAppMode('drawing'); // Default
    }

    // --- Add Text Button ---
    if (ui.btnAddText) {
        ui.btnAddText.addEventListener('click', addTextToTrajectory);
    }

    // --- Text Mode Listeners ---
    if (ui.btnPresetLetter) {
        ui.btnPresetLetter.addEventListener('click', () => {
            console.log("Applying LETTER MODE preset");
            // Switch to Linear Mode
            setTextMode('linear');

            // Set Values
            if (ui.inputFontSize) ui.inputFontSize.value = "0.01";
            if (ui.inputLinX) ui.inputLinX.value = "0.05";
            if (ui.inputLinY) ui.inputLinY.value = "0.15";
            if (ui.inputLinAngle) ui.inputLinAngle.value = "-360";

            // Trigger updates
            generatePreview();

            // Save state (debounced trigger not needed really, direct save)
            state.saveState();
        });
    }

    ui.btnModeLinear.addEventListener('click', () => setTextMode('linear'));
    ui.btnModeCurved.addEventListener('click', () => setTextMode('curved'));

    // Validate Inputs
    [ui.inputText, ui.inputFontSize, ui.inputLinX, ui.inputLinY, ui.inputLinAngle, ui.inputCurvRadius, ui.inputCurvOffset, ui.inputWsX, ui.inputWsY, ui.inputWsW, ui.inputWsH].forEach(el => {
        if (el) el.addEventListener('input', validateGeneratedPatches);
    });

    // --- Right Sidebar Toggle ---
    const rightSidebar = document.getElementById('right-sidebar');
    const rightSidebarToggle = document.getElementById('right-sidebar-toggle-btn');
    const rightSidebarClose = document.getElementById('close-right-sidebar-btn');

    if (rightSidebarToggle) {
        rightSidebarToggle.addEventListener('click', () => {
            // Only add class open if sidebar exists
            if (rightSidebar) rightSidebar.classList.add('open');
            rightSidebarToggle.style.display = 'none'; // Hide toggle
        });
    }

    if (rightSidebarClose) {
        rightSidebarClose.addEventListener('click', () => {
            if (rightSidebar) rightSidebar.classList.remove('open');
            if (rightSidebarToggle) rightSidebarToggle.style.display = 'flex'; // Show toggle
        });
    }

    // Sync Text Input to State
    // We save state only when user STOPS typing to avoid 100 history states for 1 word
    let textInputTimer = null;
    ui.inputText.addEventListener('input', () => {
        state.text = ui.inputText.value; // Realtime update

        clearTimeout(debounceTimer);
        debounceTimer = setTimeout(() => {
            generatePreview();
        }, 100);

        // Save to History (Debounced 500ms)
        clearTimeout(textInputTimer);
        textInputTimer = setTimeout(() => {
            // Only save if it's a meaningful change? 
            // StateManager logic handles duplicate states or we can check here?
            // For now, simpler: just save.
            state.saveState();
            updateUndoRedoUI();
        }, 500);
    });

    // Subscribe to state changes (Undo/Redo)
    state.subscribe((newState) => {
        // Update UI from State
        if (ui.inputText.value !== newState.text) {
            ui.inputText.value = newState.text;
        }

        // Restore Settings if they exist in state
        if (newState.textSettings) {
            const ts = newState.textSettings;
            if (ts.mode) setTextMode(ts.mode);

            if (ts.fontSize && ui.inputFontSize) ui.inputFontSize.value = ts.fontSize;

            if (ts.linX && ui.inputLinX) ui.inputLinX.value = ts.linX;
            if (ts.linY && ui.inputLinY) ui.inputLinY.value = ts.linY;
            if (ts.linAngle && ui.inputLinAngle) ui.inputLinAngle.value = ts.linAngle;

            if (ts.curvRadius && ui.inputCurvRadius) ui.inputCurvRadius.value = ts.curvRadius;
            if (ts.curvOffset && ui.inputCurvOffset) ui.inputCurvOffset.value = ts.curvOffset;

            if (ts.wsX && ui.inputWsX) ui.inputWsX.value = ts.wsX;
            if (ts.wsY && ui.inputWsY) ui.inputWsY.value = ts.wsY;
            if (ts.wsW && ui.inputWsW) ui.inputWsW.value = ts.wsW;
            if (ts.wsH && ui.inputWsH) ui.inputWsH.value = ts.wsH;
        }

        // Refresh Views
        if (state.appMode === 'text') generatePreview();
        updateUndoRedoUI();
        // Canvas is redrawn by loop, but we might want to ensure points are fresh
        // The animate loop in canvas.js reads state.points directly.
    });

    // Helper to sync text settings to state for Undo/Redo
    function syncTextSettings() {
        state.textSettings = {
            mode: state.textMode,
            fontSize: ui.inputFontSize.value,
            linX: ui.inputLinX.value,
            linY: ui.inputLinY.value,
            linAngle: ui.inputLinAngle.value,
            curvRadius: ui.inputCurvRadius.value,
            curvOffset: ui.inputCurvOffset.value,
            wsX: ui.inputWsX.value,
            wsY: ui.inputWsY.value,
            wsW: ui.inputWsW.value,
            wsH: ui.inputWsH.value
        };
    }

    // Update Text Mode to also sync settings
    const originalSetTextMode = window.setTextMode; // Assuming it's global or accessible? No, it's scoped.
    // We already found setTextMode definitions earlier. We should rely on our listeners.

    // Listeners for Parameters
    [ui.inputFontSize, ui.inputLinX, ui.inputLinY, ui.inputLinAngle, ui.inputCurvRadius, ui.inputCurvOffset, ui.inputWsX, ui.inputWsY, ui.inputWsW, ui.inputWsH].forEach(el => {
        if (el) {
            el.addEventListener('input', () => {
                clearTimeout(debounceTimer);
                debounceTimer = setTimeout(generatePreview, 100);

                // Save State on parameter change (Debounced)
                clearTimeout(textInputTimer);
                textInputTimer = setTimeout(() => {
                    syncTextSettings();
                    state.saveState();
                    updateUndoRedoUI();
                }, 500);
            });
        }
    });

    // Redundant text buttons removed

    // Workspace Inputs (Linear)
    [ui.inputWsX, ui.inputWsY, ui.inputWsW, ui.inputWsH].forEach(el => {
        if (el) {
            el.addEventListener('input', () => {
                updateWorkspaceState();
                validateText();
            });
        }
    });

    // Workspace Inputs (Curved)
    [ui.inputWsInnerR, ui.inputWsOuterR].forEach(el => {
        if (el) {
            el.addEventListener('input', () => {
                updateWorkspaceState();
            });
        }
    });

    // --- Paper Size Logic ---
    if (ui.selectPaperSize) {
        const paperSizes = {
            'A0': { w: 0.841, h: 1.189 },
            'A1': { w: 0.594, h: 0.841 },
            'A2': { w: 0.420, h: 0.594 },
            'A3': { w: 0.297, h: 0.420 },
            'A4': { w: 0.210, h: 0.297 },
            'A5': { w: 0.148, h: 0.210 },
            'F4': { w: 0.210, h: 0.330 }
        };

        ui.selectPaperSize.addEventListener('change', (e) => {
            const size = e.target.value;
            if (paperSizes[size]) {
                // Update Dimensions
                if (ui.inputWsW) ui.inputWsW.value = paperSizes[size].w.toFixed(3);
                if (ui.inputWsH) ui.inputWsH.value = paperSizes[size].h.toFixed(3);

                // Update Position (Center Y, Offset X)
                if (ui.inputWsX) ui.inputWsX.value = "0.05"; // Standard offset
                if (ui.inputWsY) ui.inputWsY.value = (-paperSizes[size].h / 2).toFixed(3); // Center Y

                // Trigger input events to ensure state updates and validation run
                if (ui.inputWsW) ui.inputWsW.dispatchEvent(new Event('input'));
                if (ui.inputWsH) ui.inputWsH.dispatchEvent(new Event('input'));
                if (ui.inputWsX) ui.inputWsX.dispatchEvent(new Event('input'));
                if (ui.inputWsY) ui.inputWsY.dispatchEvent(new Event('input'));
            }
        });

        // Set to custom if manual edit
        [ui.inputWsW, ui.inputWsH].forEach(el => {
            if (el) {
                el.addEventListener('input', () => {
                    ui.selectPaperSize.value = 'custom';
                });
            }
        });
    }

    // Motion Profile Selector
    if (ui.selectMotionProfile) {
        ui.selectMotionProfile.addEventListener('change', (e) => {
            const profile = e.target.value;
            console.log(`Switching Motion Profile to: ${profile}`);
            // Call Python Backend
            window.eel.py_set_motion_profile(profile)((res) => {
                if (res) console.log("Backend confirmed profile switch.");
                else console.error("Backend failed to switch profile.");
            });
        });
    }

    // Motion Parameters (Acc/Speed)
    [ui.inputMotionAcc, ui.inputMotionSpeed].forEach(el => {
        if (el) {
            el.addEventListener('change', () => {
                const acc = ui.inputMotionAcc.value;
                const speed = ui.inputMotionSpeed.value;
                console.log(`Setting Motion Params: Acc=${acc}, Speed=${speed}`);

                window.eel.py_set_motion_params(acc, speed)((res) => {
                    if (res) console.log("Backend confirmed params update.");
                    else console.error("Backend failed to update params.");
                });
            });
        }
    });

    // Motion Tc (Time Step)
    if (ui.inputMotionTc) {
        ui.inputMotionTc.addEventListener('change', (e) => {
            const tc = e.target.value;
            console.log(`Setting Tc to: ${tc}`);
            window.eel.py_set_tc(tc)((res) => {
                if (res) console.log("Backend confirmed Tc update.");
                else console.error("Backend failed to update Tc.");
            });
        });
    }

}

// Main Initialization
window.addEventListener('load', () => {
    console.log("Window Load - Initializing App");
    initUI();

    // Init Callbacks HERE to ensure window.eel is ready
    API.initCallbacks({
        onLog: (msg) => {
            const consoleEl = document.getElementById('console-output');
            if (consoleEl) consoleEl.textContent = msg;
        },

        onDrawPose: (q) => {
            // console.log("DEBUG: JS received pose:", q);
            if (state.manipulator) state.manipulator.q = q;
        },

        onDrawTraces: (data) => {
            if (state.manipulator && data && data.length === 2) {
                // data is [q1_array, q2_array]
                const q1 = data[0];
                const q2 = data[1];

                state.manipulator.reset_trace();

                // Iterate and add points
                // Optimization: Don't draw every single point if too many?
                // For now, draw all.
                for (let i = 0; i < q1.length; i++) {
                    state.manipulator.add2trace([q1[i], q2[i]]);
                }
            }
        },

        onGetData: () => {
            return getTrajectoryPayload();
        }
    });

    setupEventListeners();
    updateWorkspaceState();
    updateUndoRedoUI();
    if (typeof canvasHandler !== 'undefined' && canvasHandler) canvasHandler.resize();
    updateSerialStatus();
    new ThemeManager(); // Initialize Theme Manager
    setInterval(updateSerialStatus, 50);
});

// --- App Mode Logic ---

function setAppMode(mode) {
    state.appMode = mode;

    // Update Buttons
    ui.btnAppModeDrawing.classList.toggle('active', mode === 'drawing');
    ui.btnAppModeText.classList.toggle('active', mode === 'text');
    if (ui.btnAppModeImage) ui.btnAppModeImage.classList.toggle('active', mode === 'image');

    ui.sectionDrawing.style.display = (mode === 'drawing') ? 'block' : 'none';
    ui.sectionText.style.display = (mode === 'text') ? 'block' : 'none';
    if (ui.sectionImage) ui.sectionImage.style.display = (mode === 'image') ? 'block' : 'none';

    // Melody Triggers (Safe Mode: Vibration only)
    // Drawing=2, Text=3, Image=4
    let melodyId = 0;
    if (mode === 'drawing') melodyId = 2;
    else if (mode === 'text') melodyId = 3;
    else if (mode === 'image') melodyId = 4;

    if (melodyId > 0) {
        // Fire and forget
        eel.py_play_melody(melodyId)().catch(e => console.error("Melody Trigger Failed:", e));
    }

    if (mode === 'drawing') {
        // Force redraw to clear ghost
        if (typeof canvasHandler !== 'undefined' && canvasHandler) canvasHandler.animate();
    } else if (mode === 'text') {
        generatePreview();
    }
}


// Mode listener attachment moved to setupEventListeners()

// --- Text Tool Logic ---

// Default State
state.textMode = 'linear'; // 'linear' | 'curved'
state.generatedTextPatches = [];
state.accumulatedTextPatches = []; // Accumulated patches for multiple texts

// Function to add current text to the accumulated trajectory
async function addTextToTrajectory() {
    const text = ui.inputText.value;
    if (!text) {
        alert('Inserisci del testo prima di aggiungerlo!');
        return;
    }

    const options = getTextOptions();
    console.log("Adding Text to Trajectory:", text);

    try {
        const patches = await API.generateText(text, options);
        if (patches && patches.length > 0) {
            // Apply Douglas-Peucker Simplification to text patches
            // Text is small, so we can use a small tolerance like 0.0005m = 0.5mm
            // (converted to pixels internally by simplifyPath if needed, but text patches may already be in meters or pixels depending on backend).
            // Actually, API.generateText returns patches with points in meters.
            // simplifyPath works on Point objects (pixels). Let's convert if necessary.

            // Assume default tolerance for text, or fetch from UI if exists. Let's hardcode a small optimization.
            const textToleranceMeters = 0.0002; // 0.2mm
            const tolerancePixels = textToleranceMeters / state.settings.m_p;

            const optimizedPatches = patches.map(patch => {
                if (patch.type !== 'line' && patch.type !== 'polyline') return patch;

                const abstractPts = patch.points.map(p => {
                    // points are [x,y] in meters from Backend
                    const [relX, relY] = abs2rel(p[0], p[1], state.settings);
                    return new Point(relX, relY, state.settings);
                });

                const simplifiedAbstractPts = simplifyPath(abstractPts, tolerancePixels);

                return {
                    ...patch,
                    points: simplifiedAbstractPts.map(p => [p.actX, p.actY])
                };
            });

            // Accumulate optimized patches instead of replacing
            state.accumulatedTextPatches.push(...optimizedPatches);
            state.generatedTextPatches = [...state.accumulatedTextPatches];

            // Clear only the text input, keep position/angle for user to modify
            ui.inputText.value = '';

            // Clear the preview (current text only)
            state.textPreview = [];

            console.log(`Text added! Total patches: ${state.accumulatedTextPatches.length}`);

            // Trigger canvas redraw
            if (typeof canvasHandler !== 'undefined' && canvasHandler) {
                canvasHandler.animate();
            }
        }
    } catch (e) {
        console.error("Error adding text:", e);
        alert('Errore durante la generazione del testo.');
    }
}

function getTextOptions() {
    return {
        mode: state.textMode,
        fontSize: parseFloat(ui.inputFontSize.value) || 0.05,
        x: parseFloat(ui.inputLinX.value) || 0.05,
        y: parseFloat(ui.inputLinY.value) || 0.0,
        angle: parseFloat(ui.inputLinAngle.value) || 0,
        radius: parseFloat(ui.inputCurvRadius.value) || 0.2,
        offset: parseFloat(ui.inputCurvOffset.value) || 90
    };
}

// Text mode listener attachment moved to setupEventListeners()

function setTextMode(mode) {
    state.textMode = mode;

    // Update Buttons
    ui.btnModeLinear.classList.toggle('active', mode === 'linear');
    ui.btnModeCurved.classList.toggle('active', mode === 'curved');

    // Update Text Controls Visibility
    if (mode === 'linear') {
        ui.controlsLinear.classList.remove('hidden');
        ui.controlsCurved.classList.add('hidden');
    } else {
        ui.controlsLinear.classList.add('hidden');
        ui.controlsCurved.classList.remove('hidden');
    }

    // Update Workspace Geometry Controls Visibility
    if (ui.linearWsControls && ui.curvedWsControls) {
        if (mode === 'linear') {
            ui.linearWsControls.classList.remove('hidden');
            ui.curvedWsControls.classList.add('hidden');
            if (ui.paperSizeBlock) ui.paperSizeBlock.classList.remove('hidden');
        } else {
            ui.linearWsControls.classList.add('hidden');
            ui.curvedWsControls.classList.remove('hidden');
            if (ui.paperSizeBlock) ui.paperSizeBlock.classList.add('hidden');
        }
    }

    // Regenerate preview with new mode parameters
    generatePreview();

    // Re-validate with mode-specific constraints
    validateText();

    // Force Redraw of Workspace Background
    setTimeout(() => {
        if (canvasHandler) canvasHandler.animate();
    }, 10);
}

// Validate generated patches against robot reach and workspace limits
function validateGeneratedPatches() {
    if (!ui.warningMsg) return;

    const patches = state.textPreview || [];

    // No patches = no warning
    if (patches.length === 0) {
        ui.warningMsg.classList.add('hidden');
        return;
    }

    // Robot arm reach limits
    const l1 = state.settings.l1 || 0.170;
    const l2 = state.settings.l2 || 0.158;
    const maxReach = l1 + l2;
    const minReach = Math.abs(l1 - l2);

    // Counters
    let failReach = 0;
    let failWorkspace = 0;
    let totalPoints = 0;

    // Workspace Limits
    const mode = state.textMode;
    const linWs = state.settings.linearWorkspace;
    const curvWs = state.settings.curvedWorkspace;

    // Check each patch point
    for (const patch of patches) {
        // Handle both line and circle patches (circles have points too, usually sampled or start/end)
        // If it's a primitive circle, we might need to check center +/- radius?
        // textPreview usually contains SAMPLED lines or primitives.
        // API.generateText returns patches. If they are primitives, they might just have start/end.
        // For accurate validation, we might want to assume lines for text.
        // text_to_traj in char_gen can do sampling.

        let pointsToCheck = patch.points || [];

        for (const pt of pointsToCheck) {
            const x = pt[0];
            const y = pt[1];
            const distance = Math.sqrt(x * x + y * y);
            totalPoints++;

            // 1. PHYSICAL REACH (RED)
            if (distance > maxReach || distance < minReach) {
                failReach++;
            } else {
                // 2. LOGICAL WORKSPACE (YELLOW) - Only check if physically reachable
                if (mode === 'linear' && linWs) {
                    // Check if inside Rect
                    // Rect: x, y, w, h. 
                    // x,y is bottom-left? We need to clarify standard.
                    // Assuming inputs are: x, y, w, h.
                    // Valid X: [x, x+w]
                    // Valid Y: [y, y+h]
                    // Note: Y might be negative.
                    if (x < linWs.x || x > linWs.x + linWs.w ||
                        y < linWs.y || y > linWs.y + linWs.h) {
                        failWorkspace++;
                    }
                } else if (mode === 'curved' && curvWs) {
                    // Check if inside Donut
                    // Radius: [inner, outer]
                    // Angle: Standard right sector? (-90 to +90?)
                    // User sketch shows right semi-circle.
                    if (distance < curvWs.innerRadius || distance > curvWs.outerRadius) {
                        failWorkspace++;
                    }
                    if (x < 0) { // Keep to right side (hemisphere)
                        failWorkspace++;
                    }
                }
            }
        }
    }

    // Priority: Reach Error (Red) > Workspace Warning (Yellow)
    if (failReach > 0) {
        ui.warningMsg.textContent = `⚡ CRTICIAL: ${failReach} points out of robot reach!`;
        ui.warningMsg.style.color = '#ff4444'; // Red
        ui.warningMsg.style.borderColor = '#ff4444';
        ui.warningMsg.classList.remove('hidden');
    }
    else if (failWorkspace > 0) {
        ui.warningMsg.textContent = `⚠ WARNING: Text exceeds ${mode} workspace boundaries.`;
        ui.warningMsg.style.color = '#ffbb33'; // Orange/Yellow
        ui.warningMsg.style.borderColor = '#ffbb33';
        ui.warningMsg.classList.remove('hidden');
    }
    else {
        ui.warningMsg.classList.add('hidden');
    }
}

// Legacy function kept for compatibility
function validateText() {
    // Real validation happens in validateGeneratedPatches after patches are generated
}

function updateWorkspaceState() {
    // Parse Linear Workspace inputs
    const x = parseFloat(ui.inputWsX?.value) || 0.01;
    const y = parseFloat(ui.inputWsY?.value) || -0.18;
    const w = parseFloat(ui.inputWsW?.value) || 0.27;
    const h = parseFloat(ui.inputWsH?.value) || 0.36;

    // Update Linear Workspace State
    if (state.settings.linearWorkspace) {
        state.settings.linearWorkspace.x = x;
        state.settings.linearWorkspace.y = y;
        state.settings.linearWorkspace.w = w;
        state.settings.linearWorkspace.h = h;
    }

    // Parse Curved Workspace inputs
    const innerR = parseFloat(ui.inputWsInnerR?.value) || 0.10;
    const outerR = parseFloat(ui.inputWsOuterR?.value) || 0.30;

    // Update Curved Workspace State
    if (state.settings.curvedWorkspace) {
        state.settings.curvedWorkspace.innerRadius = innerR;
        state.settings.curvedWorkspace.outerRadius = outerR;
    }

    // Trigger Canvas Redraw for real-time feedback
    if (typeof canvasHandler !== 'undefined' && canvasHandler) {
        canvasHandler.animate();
    }
}

// Workspace input listeners moved to setupEventListeners()

// Initialize state from default inputs - moved to setupEventListeners/window.load
// updateWorkspaceState(); // Commented - runs in init

// Real-time Text Visualization handled in setupEventListeners()

// Validation listeners moved to setupEventListeners()

// Override Send Button to include Text if valid?
// The user request says: "Clicca Generate Text per visualizzare... Clicca Send Trajectory per avviare".
// "Send Trajectory" normally sends `state.trajectory`.
// If we generated text, should we Convert text patches to `state.trajectory`?
// Yes.
// So when clicking Generate, we should probably update the main trajectory or a separate one?
// --- Real-time Text Logic ---

let debounceTimer = null;

async function generatePreview() {
    const text = ui.inputText.value;
    if (!text) {
        state.textPreview = [];
        state.generatedTextPatches = [];
        validateText(); // Will hide warning
        return;
    }

    const options = getTextOptions();
    console.log("Generating Preview for:", text); // Debug

    try {
        const patches = await API.generateText(text, options);
        console.log("Patches:", patches ? patches.length : 0);

        state.textPreview = patches || [];
        state.generatedTextPatches = patches || [];

        // Text patches are stored separately in state.textPreview
        // They are combined with drawing trajectory only when sending (getTrajectoryPayload)

        validateGeneratedPatches(); // Validate actual coordinates against robot reach

    } catch (e) {
        console.error("Preview Generation Error:", e);
    }
}

// All event listeners for text inputs are attached in setupEventListeners().


// --- Helper Functions ---

function updateSerialStatus() {
    API.getSerialStatus().then(online => {
        state.isSerialOnline = online;
        if (online) {
            ui.statusText.textContent = "Connected";
            ui.statusText.style.color = "#00ff88"; // Neon Green
            ui.statusDot.classList.add('online');
            ui.btnConnect.disabled = true;
            ui.btnConnect.textContent = "Serial Online";
        } else {
            ui.statusText.textContent = "Disconnected (Sim Mode)";
            ui.statusText.style.color = "#666";
            ui.statusDot.classList.remove('online');
            ui.btnConnect.disabled = false;
        }
    });

    // POLLING LOOP for Simulation/Position
    // We poll faster to get smooth animation
    API.getPosition().then(pos => {
        // console.log("DEBUG: Polled Pos:", pos); // Debugging
        if (state.manipulator && pos && pos.length >= 2) {
            // Update Manipulator State (for drawing)
            // pos = [q0, q1, penUp]
            state.manipulator.q = [pos[0], pos[1]];

            // Note: Pen state from backend might be useful
            // But we trust local state for drawing logic usually.
            // For Simulation, we want to see the "Virtual" pen state?
            // Optional.
        }
    }).catch(e => console.warn("Polling Error:", e));
}

// Start Fast Polling (50ms = 20Hz)
// --- Cleanup & Helpers ---
// (Immediate calls removed to prevent crash before UI init)
// API.initCallbacks and Keydown listeners preserved




function getTrajectoryPayload() {
    // Collect data to save in the correct format for backend
    // Priority order: 
    // 1. Text (Linear by definition usually)
    // 2. Linear Drawings (Lines, Squares, Polygons)
    // 3. Curved Drawings (Circles, Semicircles)

    const payload = [];

    let startPointForOptimizer = { x: 0, y: 0 };

    // --- HOMING PATH: Add path from current robot position to origin (0,0) ---
    // This prevents the robot from "teleporting" when a new trajectory is started
    if (state.manipulator) {
        const currentQ0 = state.manipulator.q0;
        const currentQ1 = state.manipulator.q1;

        // Only proceed if we have valid joint angles (not null, undefined, or NaN)
        if (Number.isFinite(currentQ0) && Number.isFinite(currentQ1)) {
            // Calculate current position in world coordinates for optimizer
            const L1 = state.settings.l1 || 0.17;
            const L2 = state.settings.l2 || 0.17;
            const currentX = L1 * Math.cos(currentQ0) + L2 * Math.cos(currentQ0 + currentQ1);
            const currentY = L1 * Math.sin(currentQ0) + L2 * Math.sin(currentQ0 + currentQ1);

            startPointForOptimizer = { x: currentX, y: currentY };

            // Only add homing path if robot is not already at origin
            if (Math.abs(currentQ0) > 0.01 || Math.abs(currentQ1) > 0.01) {
                // Home position (q0=0, q1=0) = (L1+L2, 0)
                const homeX = L1 + L2;
                const homeY = 0;

                // Validate calculated positions are valid numbers
                if (Number.isFinite(currentX) && Number.isFinite(currentY) &&
                    Number.isFinite(homeX) && Number.isFinite(homeY)) {
                    // Add homing movement (pen up - no drawing)
                    payload.push({
                        'type': 'line',
                        'points': [[currentX, currentY], [homeX, homeY]],
                        'data': { 'penup': true }
                    });

                    // After homing, the robot is at home position, so we must start drawing from there
                    startPointForOptimizer = { x: homeX, y: homeY };

                    console.log(`Homing path added: (${currentX.toFixed(3)}, ${currentY.toFixed(3)}) -> (${homeX.toFixed(3)}, ${homeY.toFixed(3)})`);
                }
            }
        }
    }

    // 1. Add Text Patches if present
    if (state.generatedTextPatches && state.generatedTextPatches.length > 0) {
        payload.push(...state.generatedTextPatches);
    }

    // 2. Process Drawing Trajectory
    const linearPayload = [];
    const curvedPayload = [];

    if (state.trajectory && state.trajectory.data && state.trajectory.data.length > 0) {
        for (let t of state.trajectory.data) {

            // Helper to extract data based on type
            let item = null;

            if (t.type === 'line') {
                const p0 = t.data[0];
                const p1 = t.data[1];
                const penup = t.data[2];

                if (!p0 || !p1) continue;

                item = {
                    'type': 'line',
                    'points': simplifyPath([p0, p1].map(p => new Point(p.relX, p.relY, state.settings))).map(p => [p.actX, p.actY]),
                    'data': { 'penup': penup || false }
                };

                // Add to Linear
                linearPayload.push(item);

            } else if (t.type === 'circle') {
                const c = t.data[0];
                const rPixels = t.data[1];
                const r = rPixels * state.settings.m_p;
                const theta0 = t.data[2];
                const theta1 = t.data[3];
                const penup = t.data[4];
                // Start/End points stored in data[5] and data[6] (optional but good for exactness)
                let pStart = t.data[5];
                let pEnd = t.data[6];

                if (!c) continue;

                // Calculate Start/End if not stored
                if (!pStart) {
                    pStart = {
                        actX: c.actX + r * Math.cos(theta0),
                        actY: c.actY + r * Math.sin(theta0)
                    };
                }
                if (!pEnd) {
                    pEnd = {
                        actX: c.actX + r * Math.cos(theta1),
                        actY: c.actY + r * Math.sin(theta1)
                    };
                }

                // Calculate Delta Angle (True Arc Logic)
                const A = theta0 > theta1;
                const B = Math.abs(theta1 - theta0) < Math.PI;
                const ccw = (!A && !B) || (A && B);

                let delta = theta1 - theta0;
                if (ccw) {
                    if (delta <= 0) delta += 2 * Math.PI;
                } else {
                    if (delta >= 0) delta -= 2 * Math.PI;
                }

                // Construct Circle Patch (No Sampling)
                item = {
                    'type': 'circle',
                    'points': [[pStart.actX, pStart.actY], [pEnd.actX, pEnd.actY]],
                    'data': {
                        'penup': penup || false,
                        'center': [c.actX, c.actY],
                        'radius': r,
                        'angle': delta // Explicit Angle for Backend
                    }
                };

                // Add to Curved
                curvedPayload.push(item);
            }
        }
    }

    // Combine Drawings: Linear First, then Curved
    const drawingPayloadRaw = [...linearPayload, ...curvedPayload];

    // Optimize Drawing Path (FLSC + Nearest Neighbor)
    const optimizer = new TrajectoryOptimizer(state.settings);

    // If we have text already pushed, update startPointForOptimizer to end of last text path
    if (payload.length > 0) {
        const lastTextPatch = payload[payload.length - 1];
        if (lastTextPatch && lastTextPatch.points && lastTextPatch.points.length > 0) {
            const lastTextPt = lastTextPatch.points[lastTextPatch.points.length - 1];
            startPointForOptimizer = { x: lastTextPt[0], y: lastTextPt[1] };
        }
    }

    const drawingPayloadOptimized = optimizer.optimize(drawingPayloadRaw, startPointForOptimizer);

    // Merge Text and Drawings with Smart Jumps
    // We already have 'payload' containing Text.
    // We need to append 'drawingPayloadOptimized' but ensure Jumps between disjoint segments.

    // Helper to add jump if needed
    const safePush = (list, item) => {
        if (list.length > 0) {
            const last = list[list.length - 1];
            const lastEnd = last.points[1];
            const currStart = item.points[0];

            // Calc distance
            const dx = currStart[0] - lastEnd[0];
            const dy = currStart[1] - lastEnd[1];
            const dist = Math.sqrt(dx * dx + dy * dy);

            // Need to insert penup jump if distance isn't infinitesimal or if it's explicitly a penup
            // Wait, optimized drawing already inserts penup between line->curved groups!
            // But we must NOT inject arbitrary penups in the middle of a continuous text or complex shape.
            if (dist > 0.001) {
                // Insert Jump
                list.push({
                    'type': 'line',
                    'points': [lastEnd, currStart],
                    'data': { 'penup': true }
                });
            }
        }
        list.push(item);
    };

    // Apply Smart Jumps to the optimized drawing list
    const finalDrawing = [];
    for (let item of drawingPayloadOptimized) {
        // Only safePush if item is a line segment that is missing a jump.
        // Wait, optimizer creates jumps from Linear to Curved, but inside groups we might need jumps too.
        // Let's rely on safePush to do it.
        safePush(finalDrawing, item);
    }

    // Now append to main payload (which has text)
    // Actually safePush for appending drawing to payload
    for (let item of finalDrawing) {
        if (payload.length === 0) {
            payload.push(item);
        } else {
            safePush(payload, item);
        }
    }

    return payload;
}



// --- Helper Functions ---

function setTool(tool) {
    state.tool = tool;
    state.circleDefinition = []; // Reset partials
    state.rectangleStart = null; // Reset rectangle start
    state.semicircleStart = null; // Reset semicircle start
    state.fullcircleStart = null; // Reset fullcircle start
    state.shapeStart = null;
}

// clearTextState removed to allow combining Text + Drawing


function updateToolUI() {
    ui.btnLine.classList.toggle('active', state.tool === TOOLS.LINE);
    ui.btnCircle.classList.toggle('active', state.tool === TOOLS.CIRCLE);
    ui.btnSquare.classList.toggle('active', state.tool === TOOLS.SQUARE);
    ui.btnPolygon.classList.toggle('active', state.tool === TOOLS.POLYGON);
    ui.btnStar.classList.toggle('active', state.tool === TOOLS.STAR);
    ui.btnSemicircle.classList.toggle('active', state.tool === TOOLS.SEMICIRCLE);

    // New shapes
    if (ui.btnHeart) ui.btnHeart.classList.toggle('active', state.tool === TOOLS.HEART);
    if (ui.btnSpiral) ui.btnSpiral.classList.toggle('active', state.tool === TOOLS.SPIRAL);
    if (ui.btnCross) ui.btnCross.classList.toggle('active', state.tool === TOOLS.CROSS);
    if (ui.btnRhombus) ui.btnRhombus.classList.toggle('active', state.tool === TOOLS.RHOMBUS);
    if (ui.btnTriangle) ui.btnTriangle.classList.toggle('active', state.tool === TOOLS.TRIANGLE);
    if (ui.btnTriangleScalene) ui.btnTriangleScalene.classList.toggle('active', state.tool === TOOLS.TRIANGLE_SCALENE);
    if (ui.btnTriangleRight) ui.btnTriangleRight.classList.toggle('active', state.tool === TOOLS.TRIANGLE_RIGHT);
    if (ui.btnTriangleIsosceles) ui.btnTriangleIsosceles.classList.toggle('active', state.tool === TOOLS.TRIANGLE_ISOSCELES);
}

function updateUndoRedoUI() {
    ui.btnUndo.disabled = !state.canUndo();
    ui.btnRedo.disabled = !state.canRedo();
}

// Old updateSerialStatus removed (duplicate)
// setSerialUI removed (unused)

// --- API Callbacks ---

// API Callbacks moved to window.load initialization
// API.initCallbacks({ ... });

// Initial Status Check -> Moved to Init
// updateSerialStatus();
// --- Keyboard Shortcuts ---
document.addEventListener('keydown', (e) => {
    // Ignore if typing in input fields
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;

    // Ctrl/Cmd + Z: Undo
    if ((e.ctrlKey || e.metaKey) && e.key === 'z' && !e.shiftKey) {
        e.preventDefault();
        state.undo();
        updateUndoRedoUI();
    }

    // Ctrl/Cmd + Y or Ctrl/Cmd + Shift + Z: Redo
    if ((e.ctrlKey || e.metaKey) && (e.key === 'y' || (e.key === 'z' && e.shiftKey))) {
        e.preventDefault();
        state.redo();
        updateUndoRedoUI();
    }

    // Tool shortcuts (only if not holding Ctrl/Cmd)
    if (!e.ctrlKey && !e.metaKey && !e.altKey) {
        switch (e.key.toLowerCase()) {
            case 'l':
                setTool(TOOLS.LINE);
                updateToolUI();
                break;
            case 'c':
                setTool(TOOLS.CIRCLE);
                updateToolUI();
                break;
            case 'r':
                setTool(TOOLS.RECTANGLE);
                updateToolUI();
                break;
            case 'p':
                if (!ui.btnPolygon.disabled) {
                    setTool(TOOLS.POLYGON);
                    updateToolUI();
                }
                break;
            case 's':
                setTool(TOOLS.SEMICIRCLE);
                updateToolUI();
                break;
            case 'o':
                setTool(TOOLS.FULLCIRCLE);
                updateToolUI();
                break;
            case 'g':
                ui.btnGridToggle.click();
                break;
            case 'delete':
                ui.btnClear.click();
                break;
        }
    }
});



// --- Image Tool Logic ---

function setupImageEvents() {
    if (ui.btnProcessImage) {
        ui.btnProcessImage.addEventListener('click', processImage);
    }
    if (ui.btnConfirmImage) {
        ui.btnConfirmImage.addEventListener('click', confirmImage);
    }

    // Stepper Controls Logic
    document.querySelectorAll('.stepper-control button').forEach(btn => {
        btn.addEventListener('click', (e) => {
            const targetId = btn.dataset.target;
            const step = parseFloat(btn.dataset.step);
            const action = btn.dataset.action;
            const input = document.getElementById(targetId);

            if (input) {
                let currentVal = parseFloat(input.value) || 0;
                if (action === 'inc') currentVal += step;
                if (action === 'dec') currentVal -= step;

                // Round to avoid float errors
                if (targetId.includes('rotation')) {
                    input.value = currentVal; // integers
                } else {
                    input.value = currentVal.toFixed(3);
                }

                // Trigger event
                input.dispatchEvent(new Event('input'));
            }
        });
    });

    // Preview Transforms Update (Standard Inputs now)
    [ui.inputImgWidth, ui.inputImgX, ui.inputImgY, ui.inputImgRotation].forEach(el => {
        if (el) el.addEventListener('input', updateImagePreviewTransforms);
    });

    // Real-time Preview Controls (Debounced)
    const debouncedProcess = debounce(processImage, 500); // 500ms delay

    if (ui.inputImgThreshold) {
        ui.inputImgThreshold.addEventListener('input', (e) => {
            document.getElementById('val-threshold').textContent = e.target.value;
            debouncedProcess();
        });
    }
    if (document.getElementById('img-smoothing')) {
        document.getElementById('img-smoothing').addEventListener('input', (e) => {
            document.getElementById('val-smoothing').textContent = e.target.value;
            debouncedProcess();
        });
    }

    if (document.getElementById('img-optimization')) {
        document.getElementById('img-optimization').addEventListener('input', (e) => {
            document.getElementById('val-optimization').textContent = e.target.value;
        });
    }



    // Width/Height Sync Logic
    if (ui.inputImgWidth && ui.inputImgHeight && ui.inputImgLockRatio) {
        ui.inputImgWidth.addEventListener('input', (e) => {
            const w = parseFloat(e.target.value);
            if (ui.inputImgLockRatio.checked && state.imageAspectRatio) {
                ui.inputImgHeight.value = (w / state.imageAspectRatio).toFixed(3);
            }
        });

        ui.inputImgHeight.addEventListener('input', (e) => {
            const h = parseFloat(e.target.value);
            if (ui.inputImgLockRatio.checked && state.imageAspectRatio) {
                ui.inputImgWidth.value = (h * state.imageAspectRatio).toFixed(3);
            }
        });
    }

    // Image Style Listeners
    document.querySelectorAll('input[name="img-style"]').forEach(el => {
        el.addEventListener('change', processImage);
    });

    if (document.getElementById('img-invert')) {
        document.getElementById('img-invert').addEventListener('change', processImage);
    }

    if (document.getElementById('img-show-original')) {
        document.getElementById('img-show-original').addEventListener('change', (e) => {
            if (ui.imgPreviewImg) ui.imgPreviewImg.style.opacity = e.target.checked ? '0.3' : '0';
        });
    }

    // File Input Handle
    if (ui.inputImageFile) {
        ui.inputImageFile.addEventListener('change', (e) => {
            const file = e.target.files[0];
            if (file) {
                const reader = new FileReader();
                reader.onload = function (e) {
                    if (ui.imgPreviewImg) {
                        ui.imgPreviewImg.src = e.target.result;
                        ui.imgPreviewImg.style.display = 'block';

                        // Set Background Image for Canvas Overlay
                        const imgObj = new Image();
                        imgObj.src = e.target.result;
                        imgObj.onload = () => {
                            state.backgroundImage = imgObj;
                            // Calculate Aspect Ratio
                            if (imgObj.naturalWidth && imgObj.naturalHeight) {
                                state.imageAspectRatio = imgObj.naturalWidth / imgObj.naturalHeight;
                                // Auto-set Height based on current Width
                                if (ui.inputImgWidth && ui.inputImgHeight) {
                                    const w = parseFloat(ui.inputImgWidth.value) || 0.10;
                                    ui.inputImgHeight.value = (w / state.imageAspectRatio).toFixed(3);
                                }
                            }
                        };
                    }
                    if (document.getElementById('preview-msg')) {
                        document.getElementById('preview-msg').style.display = 'none';
                    }
                };
                reader.readAsDataURL(file);
            }
        });

        // Handle "Show Original" Toggle
        const toggleOriginal = document.getElementById('img-show-original');
        if (toggleOriginal) {
            toggleOriginal.addEventListener('change', (e) => {
                state.showOriginalImage = e.target.checked;
                if (ui.imgPreviewImg) ui.imgPreviewImg.style.opacity = e.target.checked ? '0.3' : '0.5'; // dimmed if checked to not distract
                if (window.canvasHandler) window.canvasHandler.animate();
            });
        }

        // Handle Global Reset Button (Context Aware)
        const btnGlobalReset = document.getElementById('btn-global-reset');
        if (btnGlobalReset) {
            btnGlobalReset.addEventListener('click', () => {
                // Determine active mode by checking button classes
                let currentMode = 'drawing'; // default
                const btnText = document.getElementById('app-mode-text');
                const btnImage = document.getElementById('app-mode-image');

                if (btnText && btnText.classList.contains('active')) currentMode = 'text';
                if (btnImage && btnImage.classList.contains('active')) currentMode = 'image';

                console.log(`Global Reset triggered for mode: ${currentMode}`);

                if (currentMode === 'image') {
                    if (confirm("Reset Image Mode Cache? (File & State cleared, Params kept)")) {
                        // Save State before clearing for Undo
                        // Note: Image state is not fully history-tracked yet (backgroundImage is ref), 
                        // but patches are. So undo might restore patches but maybe not the file input usage.
                        // Implemented for consistency.

                        // Clear Image Source
                        if (ui.inputImageFile) ui.inputImageFile.value = '';

                        // Clear Preview
                        if (ui.imgPreviewImg) {
                            ui.imgPreviewImg.src = '';
                            ui.imgPreviewImg.style.display = 'none';
                        }

                        // Clear State
                        state.backgroundImage = null;
                        state.rawImagePatches = [];
                        state.imagePreviewPatches = [];
                        state.showOriginalImage = false;

                        showImageStatus("");
                        const logContainer = document.getElementById('image-debug-log');
                        if (logContainer) logContainer.classList.add('hidden');

                        // Update Canvas
                        if (window.canvasHandler) window.canvasHandler.animate();

                        // Sound ID 4
                        if (window.eel) window.eel.py_trigger_sound(4);
                    }
                } else if (currentMode === 'text') {
                    if (confirm("Clear Text Input?")) {
                        const txtInput = document.getElementById('text-input');
                        if (txtInput) {
                            txtInput.value = '';
                            // Update state and save for Undo
                            state.text = '';
                            state.saveState();
                            // Force preview update
                            generatePreview();
                        }

                        // Sound ID 2
                        if (window.eel) window.eel.py_trigger_sound(2);
                    }
                } else {
                    // Drawing Mode (and others fallback)
                    if (confirm("Clear Drawing Canvas?")) {
                        if (window.canvasHandler) {
                            window.canvasHandler.clear(); // This usually handles saveState internally
                        }

                        // Sound ID 3
                        if (window.eel) window.eel.py_trigger_sound(3);
                    }
                }
            });
        }

        // Auto-Process on Parameter Change
        const autoProcessInputs = [
            ui.inputImgWidth,
            ui.inputImgHeight,
            ui.inputImgThreshold,
            document.getElementById('img-smoothing'),
            document.getElementById('img-invert')
        ];

        autoProcessInputs.forEach(input => {
            if (input) {
                // Use 'change' for committed changes (better for sliders/numbers to avoid too many calls)
                // For immediate response on slider drag, 'input' is better but requires debounce.
                // Given the new "kill & restart" logic, 'change' is safer for now.
                // If user wants live drag, we need a debounce wrapper.
                input.addEventListener('change', processImage);
            }
        });

        // Radio buttons for Style
        document.querySelectorAll('input[name="img-style"]').forEach(radio => {
            radio.addEventListener('change', processImage);
        });

        if (document.getElementById('img-noise-reduction')) {
            document.getElementById('img-noise-reduction').addEventListener('change', processImage);
        }
    }
}

state.rawImagePatches = []; // Patches from backend (normalized)
state.imagePreviewPatches = []; // Patches transformed for preview

let isImageProcessingRunning = false;

async function processImage() {
    const btnProcess = ui.btnProcessImage;
    const loadingEl = document.getElementById('image-loading');
    const fileInput = ui.inputImageFile;

    if (isImageProcessingRunning) {
        // Visual Feedback for Restart
        if (loadingEl) {
            loadingEl.textContent = "🔄 Riavvio...";
            loadingEl.classList.remove('hidden');
        }
        if (btnProcess) {
            btnProcess.innerHTML = "🔄 RIAVVIO...";
            btnProcess.style.opacity = "0.7";
        }

        if (window.js_log_image_processing) {
            window.js_log_image_processing("Riavvio processo con nuovi parametri...");
        } else {
            console.warn("Riavvio processo con nuovi parametri...");
        }

        // Signal backend to stop
        if (window.eel) {
            await window.eel.py_stop_processing()();
            // Give a tiny moment for backend to catch the flag
            await new Promise(r => setTimeout(r, 100));
        }
    }

    if (!fileInput || !fileInput.files || fileInput.files.length === 0) {
        // Only alert if clicked explicitly on the Process button
        // (Silence the alert if triggered by parameter changes)
        // Check if event exists and if target is the button
        // Note: debounced calls might not have the original event as first arg depending on implementation, 
        // but direct listeners will.
        // Assuming 'this' context or explicit 'e' argument.

        // Use a safe check (if e is undefined/null, it wasn't a click)
        try {
            // Check if argument 0 is an event and its target is the button
            const isButton = (arguments[0] instanceof Event) && (arguments[0].currentTarget === ui.btnProcessImage || arguments[0].target === ui.btnProcessImage);

            if (isButton) {
                alert("Please select an image file first.");
            }
        } catch (err) {
            // Ignore error
        }

        return;
    }

    // Lock execution
    isImageProcessingRunning = true;

    // Visual Feedback for Start
    if (loadingEl) {
        loadingEl.textContent = "⏳ Elaborazione...";
        loadingEl.classList.remove('hidden');
    }
    if (btnProcess) {
        btnProcess.innerHTML = "⏳ ELABORAZIONE...";
        btnProcess.disabled = true;
        btnProcess.style.cursor = "wait";
    }

    // (Loading shown above)

    // START TRY BLOCK IMMEDIATELY
    try {
        if (window.js_log_image_processing) {
            const logContainer = document.getElementById('image-debug-log');
            if (logContainer) logContainer.innerHTML = '';
            window.js_log_image_processing(">>> IN ESECUZIONE...");
        }

        const file = fileInput.files[0];
        const isSvg = file.name.toLowerCase().endsWith('.svg');

        let base64Data = "";

        if (isSvg) {
            // For SVG, read as text then resize/convert? No, backend handles SVG text.
            // readAsDataURL handles both.
            base64Data = await new Promise((resolve, reject) => {
                const reader = new FileReader();
                reader.onload = () => resolve(reader.result); // content
                reader.onerror = reject;
                reader.readAsDataURL(file);
            });
        } else {
            // For Raster, Resize Client-Side first to save bandwidth
            base64Data = await resizeImage(file, 800);
        }

        // Clear Previous Logs
        const logContainer = document.getElementById('image-debug-log');
        if (logContainer) {
            logContainer.innerHTML = '';
            logContainer.classList.remove('hidden');
        }

        const options = {
            mode: isSvg ? 'svg' : 'vector_bw', // Force Vector Mode
            style: document.querySelector('input[name="img-style"]:checked')?.value || 'auto',
            width: parseFloat(ui.inputImgWidth.value) || 0.10,
            height: parseFloat(ui.inputImgHeight.value) || 0.10,
            x: 0,
            y: 0,
            rotation: 0,
            threshold: parseInt(ui.inputImgThreshold.value) || 127,
            smoothing: parseInt(document.getElementById('img-smoothing')?.value) || 2,
            inverted: document.getElementById('img-invert')?.checked || false,
            noise_reduction: document.getElementById('img-noise-reduction')?.checked || false
        };


        // Call Backend
        const patches = await window.eel.py_process_image(base64Data, options)();

        if (patches === null) {
            console.warn("Processo interrotto dall'utente.");
            // Do not alert, do not update state
        } else if (patches) {
            console.log(`Received ${patches.length} patches from backend.`);
            state.rawImagePatches = patches;
            updateImagePreviewTransforms(); // Update preview with current transforms
            showImageStatus("IMMAGINE PROCESSATA CON SUCCESSO");
        } else {
            alert("Image processing failed or returned no paths.");
        }

    } catch (e) {
        console.error("Image Process Error:", e);
        alert("Error processing image: " + e);
    } finally {
        isImageProcessingRunning = false;

        const loadingEl = document.getElementById('image-loading');
        if (loadingEl) loadingEl.classList.add('hidden');

        const btnProcess = ui.btnProcessImage;
        if (btnProcess) {
            btnProcess.innerHTML = "⚙ PROCESS";
            btnProcess.disabled = false;
            btnProcess.style.cursor = "pointer";
            btnProcess.style.opacity = "1";
        }
    }
}

// Expose Log Function for Backend
window.js_log_image_processing = function (msg) {
    const logContainer = document.getElementById('image-debug-log');
    if (logContainer) {
        logContainer.classList.remove('hidden');

        const line = document.createElement('div');
        line.textContent = `> ${msg}`;
        line.style.fontSize = '0.9em';
        line.style.fontFamily = 'monospace';
        line.style.color = '#00ffcc';
        line.style.borderBottom = '1px solid #333';
        line.style.padding = '2px 0';

        logContainer.appendChild(line);
        logContainer.scrollTop = logContainer.scrollHeight;
    }
};

// Expose to Eel
if (window.eel) {
    window.eel.expose(window.js_log_image_processing, 'js_log_image_processing');
}

function updateImagePreviewTransforms() {
    if (!state.rawImagePatches || state.rawImagePatches.length === 0) return;

    const offX = parseFloat(ui.inputImgX.value) || 0.20;
    const offY = parseFloat(ui.inputImgY.value) || 0.0;
    const rotDeg = parseFloat(ui.inputImgRotation.value) || 0;
    const rotRad = rotDeg * (Math.PI / 180);

    // Update State Transform for Canvas drawing
    state.imageTransform = {
        x: offX,
        y: offY,
        width: parseFloat(ui.inputImgWidth.value) || 0.10,
        rotation: rotRad
    };

    const transformedPatches = [];

    state.rawImagePatches.forEach(patch => {
        // Accept both line and polyline (contours are often polylines)
        if (patch.type !== 'line' && patch.type !== 'polyline') return;

        const newPoints = patch.points.map(p => {
            const x = p[0];
            const y = p[1];

            // Rotate
            const rx = x * Math.cos(rotRad) - y * Math.sin(rotRad);
            const ry = x * Math.sin(rotRad) + y * Math.cos(rotRad);

            // Translate
            const tx = rx + offX;
            const ty = ry + offY;

            return [tx, ty];
        });

        transformedPatches.push({
            type: patch.type, // Preserve original type (line/polyline)
            points: newPoints,
            data: patch.data
        });
    });

    state.imagePreviewPatches = transformedPatches;
    if (typeof canvasHandler !== 'undefined' && canvasHandler) canvasHandler.animate();
}

// Client-side Resize for Raster
function resizeImage(file, maxDim) {
    return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = (e) => {
            const img = new Image();
            img.onload = () => {
                let w = img.width;
                let h = img.height;
                let scale = 1;
                if (w > maxDim || h > maxDim) {
                    scale = maxDim / Math.max(w, h);
                }
                w *= scale;
                h *= scale;

                const canvas = document.createElement('canvas');
                canvas.width = w;
                canvas.height = h;
                const ctx = canvas.getContext('2d');
                ctx.drawImage(img, 0, 0, w, h);
                resolve(canvas.toDataURL(file.type));
            };
            img.onerror = reject;
            img.src = e.target.result;
        };
        reader.readAsDataURL(file);
    });
}

// Override confirmImage to add to trajectory
async function confirmImage() {
    console.log("confirmImage called");

    // UI Elements
    const loadingEl = document.getElementById('image-loading');

    if (!state.imagePreviewPatches || state.imagePreviewPatches.length === 0) {
        console.warn("No image patches to confirm.");
        alert("Nessuna traiettoria da confermare. Premi PRIMA 'Process Process'!");
        return;
    }

    // Use the transformed patches
    const rawPatches = state.imagePreviewPatches;
    console.log(`Confirming ${rawPatches.length} patches...`);

    // Get Optimization Level (0-10)
    let optLevel = 8; // Default
    const optSlider = document.getElementById('img-optimization');
    if (optSlider) optLevel = parseInt(optSlider.value);

    // Map Level to Tolerance (0 = Disabled)
    let tolerance = 0;
    if (optLevel > 0) {
        tolerance = (optLevel / 10) * 0.005; // Max 0.005m
    }

    // Visual Feedback
    if (loadingEl) {
        if (tolerance > 0) loadingEl.textContent = `⏳ Ottimizzazione (Liv. ${optLevel})...`;
        else loadingEl.textContent = "⏳ Importazione Punti...";
        loadingEl.classList.remove('hidden');
    }

    let patches = rawPatches;
    if (window.eel && tolerance > 0) {
        try {
            console.log(`Requesting trajectory optimization with tolerance ${tolerance}...`);
            const optimized = await window.eel.py_optimize_trajectory(rawPatches, tolerance)();
            if (optimized && optimized.length > 0) {
                console.log(`Optimization success. Using ${optimized.length} optimized patches.`);
                patches = optimized;
            }
        } catch (e) {
            console.error("Optimization failed, using raw patches.", e);
        }
    } else {
        console.log("Optimization disabled (Level 0). Using raw patches.");
    }

    if (loadingEl) loadingEl.classList.add('hidden');
    state.saveState(); // Save before adding

    let addedCount = 0;
    patches.forEach((patch, index) => {
        const pts = patch.points;
        if (!pts || pts.length < 2) return;

        // If not the first patch, move to the start of this patch with pen up
        if (index > 0 || state.points.length > 0) {
            const [spx, spy] = abs2rel(pts[0][0], pts[0][1], state.settings);
            const startPoint = new Point(spx, spy, state.settings);

            let lastPoint = null;
            if (state.points.length > 0) {
                lastPoint = state.points[state.points.length - 1];
            }

            if (lastPoint) {
                state.trajectory.add_line(lastPoint, startPoint, true); // Pen Up Jump
            }
            state.points.push(startPoint);
        } else {
            // First point of first patch
            const [p0x, p0y] = abs2rel(pts[0][0], pts[0][1], state.settings);
            const p0 = new Point(p0x, p0y, state.settings);
            state.points.push(p0);
        }

        let simplifiedPts = pts;

        // Simplify if tolerance is set
        if (tolerance > 0) {
            // map point arrays to Point objects for simplifyPath
            const abstractPts = pts.map(p => {
                const [relX, relY] = abs2rel(p[0], p[1], state.settings);
                return new Point(relX, relY, state.settings);
            });

            // Note: tolerance here is in meters (received from optLevel).
            // simplifyPath expects pixels. So we convert tolerance to pixels.
            const tolerancePixels = tolerance / state.settings.m_p;

            const simplifiedAbstractPts = simplifyPath(abstractPts, tolerancePixels);

            // map back to physical coordinate arrays
            simplifiedPts = simplifiedAbstractPts.map(p => [p.actX, p.actY]);
        }

        for (let i = 0; i < simplifiedPts.length - 1; i++) {
            // Convert Meters (pts) to Pixels (abs2rel) because Point() expects Pixels
            const [p1x, p1y] = abs2rel(simplifiedPts[i][0], simplifiedPts[i][1], state.settings);
            const [p2x, p2y] = abs2rel(simplifiedPts[i + 1][0], simplifiedPts[i + 1][1], state.settings);

            const p1 = new Point(p1x, p1y, state.settings);
            const p2 = new Point(p2x, p2y, state.settings);

            state.trajectory.add_line(p1, p2, false); // Pen Down
            state.points.push(p2);
        }
        addedCount++;
    });



    console.log(`Confirmed ${addedCount} contours.`);
    showImageStatus("TRAIETTORIA COMPLETATA");

    // Switch back to Drawing after delay to show message
    setTimeout(() => {
        ui.btnAppModeDrawing.click();

        // Clear Preview
        state.rawImagePatches = [];
        state.imagePreviewPatches = [];
        if (ui.imgPreviewImg) ui.imgPreviewImg.src = "";

        if (canvasHandler) canvasHandler.animate();
    }, 1500);
}

function showImageStatus(msg, duration = 3000) {
    const el = document.getElementById('image-status-msg');
    if (!el) return;
    el.textContent = msg;
    el.classList.remove('hidden');
    setTimeout(() => {
        el.classList.add('hidden');
    }, duration);
}
// Helper: Debounce function
function debounce(func, wait) {
    let timeout;
    return function (...args) {
        clearTimeout(timeout);
        timeout = setTimeout(() => func.apply(this, args), wait);
    };
}

// --- Symbol Picker Logic ---
document.addEventListener('DOMContentLoaded', () => {
    const btnShowSymbols = document.getElementById('btn-show-symbols');
    const symbolPicker = document.getElementById('symbol-picker');
    const textInput = document.getElementById('text-input');

    // List of supported symbols from char_gen.py
    const supportedSymbols = [
        // Math / Logic
        '+', '-', '=', '<', '>', '*', '/', '\\', '|', '%', '^', '±', '×', '÷', '≠', '√',
        // Punctuation
        '.', ',', ':', ';', '!', '?', '"', "'", '°',
        // Brackets
        '(', ')', '[', ']', '{', '}',
        // Arrows
        '←', '↑', '→', '↓', '↔',
        // Suits & Weather
        '♠', '♣', '♥', '♦', '☀', '☁', '⚡',
        // Office & Icons
        '✉', '⚓', '★', '♪', '☺',
        // Shapes & Misc
        '□', '△', '○', '◊', '€', '£', '$', '@', '&', '#', '_'
    ];




    if (btnShowSymbols && symbolPicker && textInput) {
        // Populate Picker
        supportedSymbols.forEach(char => {
            const btn = document.createElement('button');
            btn.textContent = char;
            btn.className = 'btn sm secondary';
            btn.style.width = '100%';
            btn.style.fontFamily = 'monospace';
            btn.style.padding = '5px 0';
            btn.style.fontSize = '1.2em';

            btn.onclick = () => {
                // Insert char at cursor position
                const start = textInput.selectionStart;
                const end = textInput.selectionEnd;
                const text = textInput.value;
                const before = text.substring(0, start);
                const after = text.substring(end, text.length);

                textInput.value = before + char + after;
                textInput.selectionStart = textInput.selectionEnd = start + 1;
                textInput.focus();

                // Trigger input event for real-time preview
                textInput.dispatchEvent(new Event('input', { bubbles: true }));
            };

            symbolPicker.appendChild(btn);
        });

        // Toggle Visibility
        btnShowSymbols.onclick = () => {
            if (symbolPicker.classList.contains('hidden')) {
                symbolPicker.classList.remove('hidden');
                symbolPicker.style.display = 'grid'; // Ensure grid layout
                btnShowSymbols.classList.add('active'); // Optional styling
            } else {
                symbolPicker.classList.add('hidden');
                symbolPicker.style.display = 'none';
                btnShowSymbols.classList.remove('active');
            }
        };
    }
});

