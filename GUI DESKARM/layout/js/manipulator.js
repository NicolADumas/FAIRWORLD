import { abs2rel } from './utils.js';

export class Manipulator {
    constructor(q, settings) {
        this.q_coords = q;
        this.settings = settings;
        this.traces = { 'x1': [], 'x2': [] };
        this.penUp = true; // Default to Up

        // Calculate initial position
        const [p1, p2] = this.dk(q);
        this.p = p1;
        this.end_eff = p2;
    }

    // --- Getters & Setters ---

    set q(q) {
        // If q is array [q0, q1], use it.
        // But if we want to pass penUp, we might need a dedicated method or pass [q0, q1, penUp]
        if (q.length === 3) {
            this.penUp = Boolean(q[2]);
            this.q_coords = [q[0], q[1]];
        } else {
            this.q_coords = q;
        }

        const [p1, p2] = this.dk(this.q_coords);
        this.p = p1;
        this.end_eff = p2;
    }

    get q() {
        return this.q_coords;
    }

    // --- Trace Management ---

    add2trace(q) {
        const [x1, x2] = this.dk(q);
        this.traces['x1'].push(x1);
        this.traces['x2'].push(x2);
    }

    add_trace(points) {
        this.traces = { 'x1': [], 'x2': [] };
        for (let point of points) {
            const [p1, p2] = abs2rel(point['x'], point['y'], this.settings);
            this.traces['x1'].push(p1);
            this.traces['x2'].push(p2);
        }
    }

    reset_trace() {
        this.traces = { 'x1': [], 'x2': [] };
    }

    // --- Kinematics ---

    dk(q) {
        const l1 = this.settings['l1'];
        const l2 = this.settings['l2'];

        // Forward Kinematics
        // p1 = end of first link
        const p1_abs = [
            l1 * Math.cos(q[0]),
            l1 * Math.sin(q[0])
        ];

        // p2 = end of second link (end effector)
        const p2_abs = [
            p1_abs[0] + l2 * Math.cos(q[0] + q[1]),
            p1_abs[1] + l2 * Math.sin(q[0] + q[1])
        ];

        // Convert to canvas relative coordinates for drawing
        const p1_rel = abs2rel(p1_abs[0], p1_abs[1], this.settings);
        const p2_rel = abs2rel(p2_abs[0], p2_abs[1], this.settings);

        return [p1_rel, p2_rel];
    }

    // --- Drawing ---

    draw_pose(ctx) {
        const origin = this.settings['origin'];

        ctx.lineWidth = 4;
        ctx.lineCap = 'round';
        ctx.lineJoin = 'round';

        // Both Links Orange
        ctx.beginPath();
        ctx.strokeStyle = "#ff8800"; // Orange
        ctx.moveTo(origin.x, origin.y);
        ctx.lineTo(this.p[0], this.p[1]);
        ctx.lineTo(this.end_eff[0], this.end_eff[1]);
        ctx.stroke();
        ctx.closePath();

        // Draw Joints
        this.drawJoint(ctx, origin.x, origin.y);
        this.drawJoint(ctx, this.p[0], this.p[1]);

        // End Effector Color based on Pen State
        // Blue if Pen Up, Red if Pen Down
        const effColor = this.penUp ? '#00e5ff' : '#ff4444'; // Cyan vs Red
        this.drawJoint(ctx, this.end_eff[0], this.end_eff[1], effColor);
    }

    drawJoint(ctx, x, y, color = '#ffffff') {
        ctx.beginPath();
        ctx.fillStyle = color;
        ctx.arc(x, y, 6, 0, 2 * Math.PI);
        ctx.fill();
        ctx.closePath();
    }

    draw_traces(ctx, colors = ['rgba(0,0,255,0.3)', 'rgba(0,255,0,0.3)']) {
        // Traces are heavy, draw them efficiently
        if (this.traces['x1'].length < 1 || this.traces['x2'].length < 1) return;

        ctx.lineWidth = 3;

        // Trace 1
        ctx.beginPath();
        ctx.strokeStyle = colors[0];
        ctx.moveTo(this.traces['x1'][0][0], this.traces['x1'][0][1]);
        for (let p of this.traces['x1']) {
            ctx.lineTo(p[0], p[1]);
        }
        ctx.stroke();
        ctx.closePath();

        // Trace 2
        ctx.beginPath();
        ctx.strokeStyle = colors[1];
        ctx.moveTo(this.traces['x2'][0][0], this.traces['x2'][0][1]);
        for (let p of this.traces['x2']) {
            ctx.lineTo(p[0], p[1]);
        }
        ctx.stroke();
        ctx.closePath();
    }
}
