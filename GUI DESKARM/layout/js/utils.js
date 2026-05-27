export class Point {
    constructor(x, y, settings) {
        this.settings = settings;
        this.relative = { x, y };
        // Calculate actual coordinates immediately
        const [rx, ry] = rel2abs(x, y, settings);
        this.actual = { 'x': rx, 'y': ry, 'z': 0 };
    }

    // --- Getters & Setters ---

    get relX() { return this.relative.x; }
    get relY() { return this.relative.y; }

    set relX(x) {
        this.relative.x = x;
        this.updateActual();
    }

    set relY(y) {
        this.relative.y = y;
        this.updateActual();
    }

    get actX() { return this.actual.x; }
    get actY() { return this.actual.y; }
    get actZ() { return this.actual.z; }

    set actX(x) {
        this.actual.x = x;
        this.updateRelative();
    }

    set actY(y) {
        this.actual.y = y;
        this.updateRelative();
    }

    // --- Helpers ---

    updateActual() {
        const [rx, ry] = rel2abs(this.relative.x, this.relative.y, this.settings);
        this.actual.x = rx;
        this.actual.y = ry;
    }

    updateRelative() {
        const [rx, ry] = abs2rel(this.actual.x, this.actual.y, this.settings);
        this.relative.x = rx;
        this.relative.y = ry;
    }

    // --- Operations ---

    add(other) {
        return new Point(this.relX + other.relX, this.relY + other.relY, this.settings);
    }

    sub(other) {
        return new Point(this.relX - other.relX, this.relY - other.relY, this.settings);
    }

    mag() {
        return Math.sqrt(this.relX * this.relX + this.relY * this.relY);
    }

    scale(scalar) {
        // Scaling preserves direction but changes magnitude relative to origin (0,0) of RELATIVE space
        // Note: original implementation had some logic about rotating. 
        // Standard vector scaling:
        return new Point(this.relX * scalar, this.relY * scalar, this.settings);
    }

    // Original "scale" implementation seemed more complex, verifying basic vector math usage in app
    // The previous code:
    /*
        var rho = scalar*this.mag();
        var theta = Math.atan2(this.relY, this.relX);
        result.relX = rho*Math.cos(theta);
        ...
    */
    // That is equivalent to simple multiplication if relative space is cartesian.

    rot(delta) {
        const rho = this.mag();
        const theta = Math.atan2(this.relY, this.relX) + delta;
        return new Point(rho * Math.cos(theta), rho * Math.sin(theta), this.settings);
    }

    set(scalar) {
        const rho = scalar;
        const theta = Math.atan2(this.relY, this.relX);
        return new Point(rho * Math.cos(theta), rho * Math.sin(theta), this.settings);
    }

    angle() {
        return Math.atan2(this.relY, this.relX);
    }
}

// Global helper functions (converted from cnv.js)

export function rel2abs(x, y, settings) {
    var x_a = (x - settings['origin']['x']) * settings['m_p'];
    var y_a = -(y - settings['origin']['y']) * settings['m_p'];
    return [x_a, y_a];
}

export function abs2rel(x, y, settings) {
    if (!settings || !settings.m_p) return [0, 0]; // Safety
    var x_p = x / settings['m_p'] + settings['origin']['x'];
    var y_p = -y / settings['m_p'] + settings['origin']['y'];
    return [x_p, y_p];
}

export function find_circ(points, circle_definition, settings) {
    /*
    Calculate circle parameters given 3 points definition logic
    */
    const n = points.length;
    const a = points[n - 1]; // starting point
    const b = circle_definition[1]; // defines diameter end relative to center?
    const k = circle_definition[0]; // defines center/radius

    // Logic from original find_circ
    // use a and k to define the center and the radius
    const r = k.sub(a).mag() / 2; // radius
    const c = a.add(k.sub(a).scale(0.5)); // center

    // p = c + (b-c) set length to r
    const p = c.add(b.sub(c).set(r)); // final point of the arc

    const v1 = c.sub(a); // vector center->start
    const v2 = c.sub(p); // vector center->end

    const theta_0 = v1.angle() + Math.PI;
    const theta_1 = v2.angle() + Math.PI;

    return { c, a, p, r, theta_0, theta_1 };
}
