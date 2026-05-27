import { Manipulator } from './manipulator.js';
import { Trajectory } from './trajectory.js';
import { Point } from './utils.js'; // Import Point for restoration

export const TOOLS = {
    LINE: 'line',
    SEMICIRCLE: 'semicircle',
    CIRCLE: 'circle',
    SQUARE: 'square',
    POLYGON: 'polygon',
    STAR: 'star',
    HEART: 'heart',
    SPIRAL: 'spiral',
    CROSS: 'greek_cross',
    RHOMBUS: 'rhombus',
    TRIANGLE: 'triangle',
    TRIANGLE_SCALENE: 'triangle_scalene',
    TRIANGLE_RIGHT: 'triangle_right',
    TRIANGLE_ISOSCELES: 'triangle_isosceles'
};

class StateManager {
    constructor() {
        this.settings = {
            'origin': { 'x': 350, 'y': 350 }, // Default, updated on resize/init
            'm_p': (0.272 * 2) / 700, // meters per pixel
            'l1': 0.128,
            'l2': 0.144,
            's_step': 1 / 50,
            'framerate': 60,
            'Tc': 0.01,
            'max_acc': 0.1,
            'max_speed': 10.0,

            // Workspace Config
            'linearWorkspace': {
                'x': 0.05,
                'y': -0.15,
                'w': 0.20,
                'h': 0.30
            },
            'curvedWorkspace': {
                'innerRadius': 0.13,
                'outerRadius': 0.27
            }
        };

        this.points = [];
        this.sentPoints = [];
        this.circleDefinition = [];

        this.tool = TOOLS.LINE;
        this.appMode = 'drawing';
        this.drawingMode = 'continuous';
        this.eraseMode = false;
        this.penUp = false;

        this.isSerialOnline = false;

        this.manipulator = null;
        this.trajectory = null;
        this.sentTrajectory = null;

        // Text State
        this.text = '';
        this.textSettings = {};

        // History for Undo/Redo
        this.history = [];
        this.historyIndex = -1;
        this.maxHistorySize = 50;

        // Observers
        this.listeners = [];

        // Grid settings
        this.snapToGrid = false;
        this.gridSize = 20; // pixels
        this.showGrid = false;
        this.showManipulator = true;
        this.showWorkspace = true;

        // Tool-specific options
        this.polygonSides = 6;
        this.arcSegments = 20;
        this.freehandPoints = [];
        this.rectangleStart = null;
        this.semicircleStart = null;
        this.fullcircleStart = null;
        this.shapeStart = null;

        // Image Tracing
        this.backgroundImage = null;
        this.showOriginalImage = true;
    }

    init(canvasWidth, canvasHeight) {
        this.settings.origin.x = canvasWidth / 2;
        this.settings.origin.y = canvasHeight / 2;
        this.settings.m_p = (0.272 * 2) / canvasWidth;

        this.manipulator = new Manipulator([0, 0], this.settings);
        this.trajectory = new Trajectory();
        this.sentTrajectory = new Trajectory();
    }

    subscribe(callback) {
        this.listeners.push(callback);
    }

    notifyListeners() {
        this.listeners.forEach(cb => cb(this));
    }

    resetWorkspace() {
        // Save state BEFORE clearing
        this.saveState();

        this.points = [];
        this.trajectory = new Trajectory();
        this.circleDefinition = [];
        this.text = '';
        // textSettings are preserved often, but let's keep them

        // Save state AFTER clearing? No, standard is Save Before Action, then Action.
        // But for "Clear", the action is "Make Empty". 
        // So we just saved the "Filled" state. Now we empty it.
        // We also need to save the "Empty" state so we can Redo to it?
        // Usually: State 1 (Filled) -> Action Clear -> State 2 (Empty).
        // If I Undo from State 2, I go to State 1.
        // So I need to push State 2 to history?
        // Let's just push current state (Filled) now. The "Empty" state is the active state.

        this.notifyListeners();
    }

    resetDrawing() {
        this.points = [];
        this.trajectory = new Trajectory();
        this.circleDefinition = [];
    }

    moveToSent() {
        this.sentPoints = [...this.points];
        this.sentTrajectory.data = [...this.trajectory.data];
        this.resetDrawing();
        this.text = '';
    }

    // --- Robust Save/Restore with Reference Preservation ---

    saveState() {
        // 1. Map current Points to Indices to preserve graph structure
        const pointToIndex = new Map();
        this.points.forEach((p, i) => pointToIndex.set(p, i));

        // 2. Serialize Trajectory using Indices
        const serializedTrajectory = this.trajectory.data.map(item => {
            const newItem = { type: item.type, data: [], groupId: item.groupId };
            // item.data contains Points and Scalars (radius/angles)
            if (item.type === 'line') {
                // [p0, p1, raised]
                newItem.data = [
                    pointToIndex.get(item.data[0]), // Index of p0
                    pointToIndex.get(item.data[1]), // Index of p1
                    item.data[2] // raised (boolean)
                ];
            } else if (item.type === 'circle') {
                // [center, radius, startAngle, endAngle, raised, startPoint, endPoint]
                // radius, angles, raised are scalars/primitives. Points need mapping.
                newItem.data = [
                    pointToIndex.get(item.data[0]), // Index of center
                    item.data[1], // radius
                    item.data[2], // startAngle
                    item.data[3], // endAngle
                    item.data[4], // raised
                    item.data[5] ? pointToIndex.get(item.data[5]) : null, // startPoint (optional/derived)
                    item.data[6] ? pointToIndex.get(item.data[6]) : null  // endPoint (optional/derived)
                ];
            }
            return newItem;
        });

        // 3. Serialize Points (Data only) - Use Relative for consistency
        const serializedPoints = this.points.map(p => ({ x: p.relX, y: p.relY }));

        // 4. Create Snapshot
        const stateSnapshot = {
            points: serializedPoints,
            trajectory: serializedTrajectory,
            // Text is simple
            text: this.text,
            textSettings: { ...this.textSettings },
            penUp: this.penUp,
            tool: this.tool
        };

        // Manage History Stack
        this.history = this.history.slice(0, this.historyIndex + 1);
        this.history.push(stateSnapshot);
        this.historyIndex++;

        if (this.history.length > this.maxHistorySize) {
            this.history.shift();
            this.historyIndex--;
        }
    }

    restoreState(snapshot) {
        if (!snapshot) return;

        // 1. Rehydrate Points
        // We MUST use the SAME settings object so they share origin/scale
        this.points = snapshot.points.map(pData => new Point(pData.x, pData.y, this.settings));

        // 2. Rehydrate Trajectory (Resolve Indices)
        this.trajectory = new Trajectory(); // Fresh container
        this.trajectory.data = snapshot.trajectory.map(item => {
            const restoredItem = { type: item.type, data: [], groupId: item.groupId };

            if (item.type === 'line') {
                // [idx0, idx1, raised]
                const p0 = this.points[item.data[0]];
                const p1 = this.points[item.data[1]];
                // Safety check
                if (p0 && p1) {
                    restoredItem.data = [p0, p1, item.data[2]];
                }
            } else if (item.type === 'circle') {
                // [centerIdx, r, t0, t1, raised, startIdx, endIdx]
                const center = this.points[item.data[0]];
                // indices 5 and 6 might be null (legacy or derived)
                const startP = (item.data[5] !== undefined && item.data[5] !== null) ? this.points[item.data[5]] : null;
                const endP = (item.data[6] !== undefined && item.data[6] !== null) ? this.points[item.data[6]] : null;

                if (center) {
                    restoredItem.data = [
                        center,
                        item.data[1], // radius
                        item.data[2], // t0
                        item.data[3], // t1
                        item.data[4], // raised
                        startP,
                        endP
                    ];
                }
            }
            return restoredItem;
        }).filter(item => item.data.length > 0); // Filter out invalid items

        // 3. Restore Other Logic
        this.text = snapshot.text || '';
        this.textSettings = snapshot.textSettings || {};
        this.penUp = snapshot.penUp;
        this.tool = snapshot.tool;

        // Reset ephemeral state
        this.shapeStart = null;
        this.semicircleStart = null;

        this.notifyListeners();
    }

    canUndo() {
        return this.historyIndex > 0;
    }

    canRedo() {
        return this.historyIndex < this.history.length - 1;
    }

    undo() {
        if (this.canUndo()) {
            // Save CURRENT state to history if we haven't?
            // "Undo" implies moving BACK.
            // When we do an action, we push S1. Index points to S1.
            // Undo -> Index points to S0. Restore S0.
            // If we are at "tip", the current visible state might be uncommitted?
            // Usually, standard Undo requires committing state BEFORE action.
            // My saveState is called inside actions (e.g. add_line).
            // So history[historyIndex] IS the current state.
            // Undo means go to historyIndex - 1.

            this.historyIndex--;
            this.restoreState(this.history[this.historyIndex]);
        }
    }

    redo() {
        if (this.canRedo()) {
            this.historyIndex++;
            this.restoreState(this.history[this.historyIndex]);
        }
    }
}

export const appState = new StateManager();
