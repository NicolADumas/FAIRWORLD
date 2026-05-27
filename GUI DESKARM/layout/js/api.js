// api.js - Wraps Eel calls

export const API = {
    async getSerialStatus() {
        if (!window.eel) return false;
        return await window.eel.py_serial_online()();
    },

    async startSerial() {
        if (!window.eel) return;
        await window.eel.py_serial_startup()();
    },

    async getPosition() {
        if (!window.eel) return [0, 0, false];
        return await window.eel.py_get_position()();
    },

    async homing() {
        if (!window.eel) return;
        await window.eel.py_homing_cmd()();
    },

    async sendData(payload) {
        // payload is passed via side-channel in original code?
        // Original code: eel.py_get_data() calls js_get_data() callback.
        // We need to maintain this flow or invert it.
        // Current flow: Frontend calls py_get_data(), which calls js_get_data() synchronously/callback, 
        // then Python processes the return value of js_get_data.
        if (!window.eel) return;
        await window.eel.py_get_data()();
    },

    async generateText(text, options) {
        if (!window.eel) return [];
        return await window.eel.py_generate_text(text, options)();
    },

    async validateText(text, options) {
        if (!window.eel) return { valid: false, message: "API Error" };
        return await window.eel.py_validate_text(text, options)();
    },

    async clearState() {
        if (!window.eel) return false;
        return await window.eel.py_clear_state()();
    },

    async saveTemplate(filename, data) {
        if (!window.eel) return false;
        return await window.eel.py_save_template(filename, data)();
    },

    async loadTemplate(filename) {
        if (!window.eel) return null;
        return await window.eel.py_load_template(filename)();
    },

    async listTemplates() {
        if (!window.eel) return [];
        return await window.eel.py_list_templates()();
    },

    async deleteTemplate(filename) {
        if (!window.eel) return { success: false, message: "Eel not available" };
        return await window.eel.py_delete_template(filename)();
    },

    async stopTrajectory() {
        if (!window.eel) return false;
        return await window.eel.py_stop_trajectory()();
    },

    async savePlots(modeName) {
        if (!window.eel) return false;
        return await window.eel.py_save_plots(modeName)();
    },

    async importDXF(base64Data) {
        if (!window.eel) return { success: false, message: "Eel not available" };
        return await window.eel.py_import_dxf(base64Data)();
    },

    // Setup callbacks that Python calls
    initCallbacks(callbacks) {
        if (!window.eel) {
            console.warn("Eel not initialized (mock mode?)");
            return;
        }

        window.js_log = (msg) => {
            console.log("[PY]", msg);
            if (callbacks.onLog) callbacks.onLog(msg);
        };

        window.js_draw_pose = (q) => {
            if (callbacks.onDrawPose) callbacks.onDrawPose(q);
        };

        window.js_draw_traces = (points) => {
            if (callbacks.onDrawTraces) callbacks.onDrawTraces(points);
        }

        // This is complex: Python calls this to GET data.
        // It expects a synchronous return of the data.
        window.js_get_data = () => {
            if (callbacks.onGetData) return callbacks.onGetData();
            return [];
        };

        // Explicitly expose them to Eel
        // Note: Eel expects the function to be on window[name] usually?
        // Or passed directly. 
        // IMPORTANT: window.eel.expose(func, name) registers 'name' callable from Python

        try {
            window.eel.expose(window.js_log, 'js_log');
            window.eel.expose(window.js_draw_pose, 'js_draw_pose');
            window.eel.expose(window.js_draw_traces, 'js_draw_traces');
            window.eel.expose(window.js_get_data, 'js_get_data');
            console.log("Eel functions exposed successfully");
        } catch (e) {
            console.error("Failed to expose Eel functions:", e);
        }
    }
};
