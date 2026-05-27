// utils_drawing.js - Helper functions for drawing tools

import { Point } from './utils.js';

/**
 * Calculate points for a rectangle given two opposite corners
 * @param {Point} p1 - First corner
 * @param {Point} p2 - Opposite corner
 * @param {Object} settings - Canvas settings
 * @returns {Point[]} Array of 4 corner points in order
 */
export function calculateRectangle(p1, p2, settings) {
    // Create the 4 corners of the rectangle
    // p1 (top-left), p2 (bottom-right in canvas coords)
    const corners = [
        p1, // Start point
        new Point(p2.relX, p1.relY, settings), // Top-right
        p2, // Bottom-right
        new Point(p1.relX, p2.relY, settings), // Bottom-left
    ];

    return corners;
}

/**
 * Calculate points for a regular polygon
 * @param {Point} center - Center point
 * @param {number} radius - Radius in pixels
 * @param {number} sides - Number of sides
 * @param {number} rotation - Rotation angle in radians (default 0)
 * @param {Object} settings - Canvas settings
 * @returns {Point[]} Array of vertex points
 */
export function calculatePolygon(center, radius, sides, rotation = 0, settings) {
    const points = [];
    const angleStep = (2 * Math.PI) / sides;

    for (let i = 0; i < sides; i++) {
        const angle = i * angleStep + rotation;
        const x = center.relX + radius * Math.cos(angle);
        const y = center.relY + radius * Math.sin(angle);
        points.push(new Point(x, y, settings));
    }

    return points;
}

/**
 * Calculate points for an ellipse
 * @param {Point} center - Center point
 * @param {number} radiusX - Horizontal radius in pixels
 * @param {number} radiusY - Vertical radius in pixels
 * @param {number} segments - Number of segments to approximate ellipse
 * @param {Object} settings - Canvas settings
 * @returns {Point[]} Array of points approximating the ellipse
 */
export function calculateEllipse(center, radiusX, radiusY, segments = 36, settings) {
    const points = [];
    const angleStep = (2 * Math.PI) / segments;

    for (let i = 0; i <= segments; i++) {
        const angle = i * angleStep;
        const x = center.relX + radiusX * Math.cos(angle);
        const y = center.relY + radiusY * Math.sin(angle);
        points.push(new Point(x, y, settings));
    }

    return points;
}

/**
 * Calculate points for an arc
 * @param {Point} center - Center point
 * @param {number} radius - Radius in pixels
 * @param {number} startAngle - Start angle in radians
 * @param {number} endAngle - End angle in radians
 * @param {number} segments - Number of segments
 * @param {Object} settings - Canvas settings
 * @returns {Point[]} Array of points approximating the arc
 */
export function calculateArc(center, radius, startAngle, endAngle, segments = 20, settings) {
    const points = [];

    // Calculate the angular span
    let span = endAngle - startAngle;
    if (span < 0) span += 2 * Math.PI;

    const angleStep = span / segments;

    for (let i = 0; i <= segments; i++) {
        const angle = startAngle + i * angleStep;
        const x = center.relX + radius * Math.cos(angle);
        const y = center.relY + radius * Math.sin(angle);
        points.push(new Point(x, y, settings));
    }

    return points;
}

/**
 * Simplify a path using the Douglas-Peucker algorithm
 * @param {Point[]} points - Array of points
 * @param {number} tolerance - Tolerance in pixels
 * @returns {Point[]} Simplified array of points
 */
export function simplifyPath(points, tolerance = 2.0) {
    if (points.length <= 2) return points;

    // Find the point with maximum distance from the line segment
    let maxDist = 0;
    let maxIndex = 0;
    const end = points.length - 1;

    for (let i = 1; i < end; i++) {
        const dist = perpendicularDistance(points[i], points[0], points[end]);
        if (dist > maxDist) {
            maxDist = dist;
            maxIndex = i;
        }
    }

    // If max distance is greater than tolerance, recursively simplify
    if (maxDist > tolerance) {
        const left = simplifyPath(points.slice(0, maxIndex + 1), tolerance);
        const right = simplifyPath(points.slice(maxIndex), tolerance);

        // Concatenate results, removing duplicate middle point
        return left.slice(0, -1).concat(right);
    } else {
        // Return just the endpoints
        return [points[0], points[end]];
    }
}

/**
 * Calculate perpendicular distance from point to line segment
 * @param {Point} point - The point
 * @param {Point} lineStart - Start of line segment
 * @param {Point} lineEnd - End of line segment
 * @returns {number} Distance in pixels
 */
function perpendicularDistance(point, lineStart, lineEnd) {
    const dx = lineEnd.relX - lineStart.relX;
    const dy = lineEnd.relY - lineStart.relY;

    // If line segment is actually a point
    if (dx === 0 && dy === 0) {
        return Math.sqrt(
            Math.pow(point.relX - lineStart.relX, 2) +
            Math.pow(point.relY - lineStart.relY, 2)
        );
    }

    // Calculate perpendicular distance
    const numerator = Math.abs(
        dy * point.relX - dx * point.relY +
        lineEnd.relX * lineStart.relY -
        lineEnd.relY * lineStart.relX
    );
    const denominator = Math.sqrt(dx * dx + dy * dy);

    return numerator / denominator;
}

/**
 * Snap a coordinate to grid
 * @param {number} value - Coordinate value
 * @param {number} gridSize - Grid size
 * @returns {number} Snapped value
 */
export function snapToGrid(value, gridSize) {
    return Math.round(value / gridSize) * gridSize;
}

/**
 * Snap a point to grid
 * @param {Point} point - Point to snap
 * @param {number} gridSize - Grid size in pixels
 * @param {Object} settings - Canvas settings
 * @returns {Point} New snapped point
 */
export function snapPointToGrid(point, gridSize, settings) {
    const snappedX = snapToGrid(point.relX, gridSize);
    const snappedY = snapToGrid(point.relY, gridSize);
    return new Point(snappedX, snappedY, settings);
}

/**
 * Calculate points for a star
 * @param {Point} center - Center point
 * @param {number} radius - Outer radius in pixels
 * @param {number} points - Number of star points
 * @param {number} innerRadiusRatio - Ratio of inner to outer radius (0-1)
 * @param {number} rotation - Rotation angle in radians
 * @param {Object} settings - Canvas settings
 * @returns {Point[]} Array of vertex points (outer, inner, outer, inner...)
 */
export function calculateStar(center, radius, points, innerRadiusRatio = 0.5, rotation = 0, settings) {
    const starPoints = [];
    const totalPoints = points * 2; // Outer + Inner
    const angleStep = Math.PI / points; // Half of (2PI / points)

    const innerRadius = radius * innerRadiusRatio;

    for (let i = 0; i < totalPoints; i++) {
        const isOuter = i % 2 === 0;
        const r = isOuter ? radius : innerRadius;
        const angle = i * angleStep + rotation;

        const x = center.relX + r * Math.cos(angle);
        const y = center.relY + r * Math.sin(angle);
        starPoints.push(new Point(x, y, settings));
    }

    return starPoints;
}

/**
 * Calculate points for a heart shape
 * @param {Point} center - Center point
 * @param {number} size - Size of the heart in pixels
 * @param {Object} settings - Canvas settings
 * @returns {Point[]} Array of points approximating the heart
 */
export function calculateHeart(center, size, settings) {
    const points = [];
    const segments = 100;

    for (let i = 0; i <= segments; i++) {
        const t = (i / segments) * 2 * Math.PI;

        // Parametric heart equation
        const x = 16 * Math.pow(Math.sin(t), 3);
        const y = -(13 * Math.cos(t) - 5 * Math.cos(2 * t) - 2 * Math.cos(3 * t) - Math.cos(4 * t));

        // Scale and translate
        const scale = size / 20;
        const px = center.relX + x * scale;
        const py = center.relY + y * scale;

        points.push(new Point(px, py, settings));
    }

    return points;
}

/**
 * Calculate points for a spiral
 * @param {Point} center - Center point
 * @param {number} radius - Maximum radius in pixels
 * @param {number} turns - Number of turns
 * @param {Object} settings - Canvas settings
 * @returns {Point[]} Array of points approximating the spiral
 */
export function calculateSpiral(center, radius, turns = 3, settings) {
    const points = [];
    const segments = turns * 50; // More points for smoother spiral

    for (let i = 0; i <= segments; i++) {
        const t = (i / segments) * turns * 2 * Math.PI;
        const r = (i / segments) * radius; // Radius grows linearly

        const x = center.relX + r * Math.cos(t);
        const y = center.relY + r * Math.sin(t);

        points.push(new Point(x, y, settings));
    }

    return points;
}

/**
 * Calculate points for a Greek cross (equal arms)
 * @param {Point} center - Center point
 * @param {number} size - Size of the cross in pixels
 * @param {number} thickness - Thickness of the arms as ratio of size (0-1)
 * @param {Object} settings - Canvas settings
 * @returns {Point[]} Array of points forming the cross outline
 */
export function calculateCross(center, size, thickness = 0.3, settings) {
    const points = [];
    const halfSize = size / 2;
    const halfThickness = (size * thickness) / 2;

    // Create cross outline (12 points, clockwise from top-right)
    const coords = [
        // Right arm top
        [halfThickness, -halfSize],
        [halfThickness, -halfThickness],
        // Top arm right
        [halfSize, -halfThickness],
        [halfSize, halfThickness],
        // Right arm bottom
        [halfThickness, halfThickness],
        [halfThickness, halfSize],
        // Bottom arm right
        [-halfThickness, halfSize],
        [-halfThickness, halfThickness],
        // Left arm bottom
        [-halfSize, halfThickness],
        [-halfSize, -halfThickness],
        // Bottom arm left
        [-halfThickness, -halfThickness],
        [-halfThickness, -halfSize]
    ];

    coords.forEach(([x, y]) => {
        points.push(new Point(center.relX + x, center.relY + y, settings));
    });

    return points;
}

/**
 * Calculate points for a rhombus (diamond shape)
 * @param {Point} center - Center point
 * @param {number} size - Size (diagonal) in pixels
 * @param {number} rotation - Rotation angle in radians
 * @param {Object} settings - Canvas settings
 * @returns {Point[]} Array of 4 points forming the rhombus
 */
export function calculateRhombus(center, size, rotation = 0, settings) {
    const points = [];
    const halfDiag1 = size / 2; // Horizontal diagonal half
    const halfDiag2 = size * 0.6; // Vertical diagonal half (60% for rhombus look)

    // 4 vertices: top, right, bottom, left
    const coords = [
        [0, -halfDiag2],  // Top
        [halfDiag1, 0],   // Right
        [0, halfDiag2],   // Bottom
        [-halfDiag1, 0]   // Left
    ];

    coords.forEach(([x, y]) => {
        // Apply rotation
        const rx = x * Math.cos(rotation) - y * Math.sin(rotation);
        const ry = x * Math.sin(rotation) + y * Math.cos(rotation);
        points.push(new Point(center.relX + rx, center.relY + ry, settings));
    });

    return points;
}

/**
 * Calculate points for an equilateral triangle
 * @param {Point} center - Center point
 * @param {number} size - Size (radius) in pixels
 * @param {number} rotation - Rotation angle in radians
 * @param {Object} settings - Canvas settings
 * @returns {Point[]} Array of 3 points forming the triangle
 */
export function calculateEquilateralTriangle(center, size, rotation = 0, settings) {
    const points = [];
    const angleOffset = -Math.PI / 2 + rotation; // Start from top

    for (let i = 0; i < 3; i++) {
        const angle = angleOffset + (i * 2 * Math.PI / 3);
        const x = center.relX + size * Math.cos(angle);
        const y = center.relY + size * Math.sin(angle);
        points.push(new Point(x, y, settings));
    }

    return points;
}

/**
 * Calculate points for a scalene triangle (user-defined with prompts)
 * For now, creates a default asymmetric triangle
 * @param {Point} center - Center point
 * @param {number} size - Size in pixels
 * @param {number} rotation - Rotation angle in radians
 * @param {Object} settings - Canvas settings
 * @returns {Point[]} Array of 3 points forming the triangle
 */
export function calculateScaleneTriangle(center, size, rotation = 0, settings) {
    const points = [];

    // Asymmetric triangle vertices (relative to center)
    const coords = [
        [0, -size * 0.7],       // Top (offset)
        [size * 0.8, size * 0.5],  // Bottom right
        [-size * 0.5, size * 0.4]  // Bottom left
    ];

    coords.forEach(([x, y]) => {
        const rx = x * Math.cos(rotation) - y * Math.sin(rotation);
        const ry = x * Math.sin(rotation) + y * Math.cos(rotation);
        points.push(new Point(center.relX + rx, center.relY + ry, settings));
    });

    return points;
}

/**
 * Calculate points for a right triangle
 * @param {Point} center - Center point
 * @param {number} size - Size in pixels
 * @param {number} rotation - Rotation angle in radians
 * @param {Object} settings - Canvas settings
 * @returns {Point[]} Array of 3 points forming the right triangle
 */
export function calculateRightTriangle(center, size, rotation = 0, settings) {
    const points = [];

    // Right triangle with right angle at bottom-left
    const coords = [
        [-size * 0.5, -size * 0.5],  // Top left (right angle vertex)
        [-size * 0.5, size * 0.5],   // Bottom left
        [size * 0.5, size * 0.5]     // Bottom right
    ];

    coords.forEach(([x, y]) => {
        const rx = x * Math.cos(rotation) - y * Math.sin(rotation);
        const ry = x * Math.sin(rotation) + y * Math.cos(rotation);
        points.push(new Point(center.relX + rx, center.relY + ry, settings));
    });

    return points;
}

/**
 * Calculate points for an isosceles triangle (two equal sides)
 * @param {Point} center - Center point
 * @param {number} size - Size in pixels
 * @param {number} rotation - Rotation angle in radians
 * @param {Object} settings - Canvas settings
 * @returns {Point[]} Array of 3 points forming the isosceles triangle
 */
export function calculateIsoscelesTriangle(center, size, rotation = 0, settings) {
    const points = [];

    // Isosceles triangle: apex at top, base wider at bottom
    const coords = [
        [0, -size * 0.7],           // Top apex
        [size * 0.5, size * 0.4],   // Bottom right
        [-size * 0.5, size * 0.4]   // Bottom left
    ];

    coords.forEach(([x, y]) => {
        const rx = x * Math.cos(rotation) - y * Math.sin(rotation);
        const ry = x * Math.sin(rotation) + y * Math.cos(rotation);
        points.push(new Point(center.relX + rx, center.relY + ry, settings));
    });

    return points;
}
