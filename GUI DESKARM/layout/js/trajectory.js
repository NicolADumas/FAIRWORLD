export class Trajectory {
    constructor() {
        this.data = [];
    }

    add_line(p0, p1, raised, groupId = null) {
        this.data.push({
            'type': 'line',
            'data': [p0, p1, raised], // start, end, raised
            'groupId': groupId
        });
    }

    add_circle(c, r, theta_0, theta_1, raised, a, p, groupId = null) {
        this.data.push({
            'type': 'circle',
            'data': [c, r, theta_0, theta_1, raised, a, p],
            'groupId': groupId
        });
    }

    reset() {
        this.data = [];
    }

    draw(ctx) {
        for (let traj of this.data) {
            const raised = (traj.type === 'line') ? traj.data[2] : traj.data[4];

            // Skip raised (pen up) segments for visualization if desired, 
            // or draw them dashed
            if (raised) {
                // Optional: Draw dashed line for pen up? 
                // Currently skipping as per original logic
                continue;
            }

            ctx.beginPath();
            ctx.lineWidth = 4;
            ctx.strokeStyle = "#000000"; // Path color - black (requested)

            if (traj.type === 'line') {
                const p0 = traj.data[0];
                const p1 = traj.data[1];
                ctx.moveTo(p0.relX, p0.relY);
                ctx.lineTo(p1.relX, p1.relY);
            } else if (traj.type === 'circle') {
                const c = traj.data[0];
                const r = traj.data[1];
                const theta_0 = traj.data[2];
                const theta_1 = traj.data[3];

                // Determine counter-clockwise check logic (from original)
                const A = theta_0 > theta_1;
                const B = Math.abs(theta_1 - theta_0) < Math.PI;
                const ccw = (!A && !B) || (A && B); // XNOR

                ctx.arc(c.relX, c.relY, r, theta_0, theta_1, ccw);
            }
            ctx.stroke();
            ctx.closePath();
        }
    }
}
