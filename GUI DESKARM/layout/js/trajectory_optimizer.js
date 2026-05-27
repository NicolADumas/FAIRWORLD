/**
 * Trajectory Optimizer - FLSC (First Linear, Second Curved) System
 * 
 * This module implements intelligent trajectory ordering and optimization:
 * 1. Classifies trajectories into Linear vs Curved
 * 2. Executes ALL linear trajectories first (faster, more precise)
 * 3. Executes ALL curved trajectories second (more complex)
 * 4. Optimizes path within each group using Nearest Neighbor algorithm
 * 5. Inserts smart connections between groups
 */

export class TrajectoryOptimizer {
    constructor(settings = {}) {
        this.settings = settings;
    }

    /**
     * Main optimization entry point
     * @param {Array} patches - Raw patches from text + drawing
     * @param {Object} startPoint - Starting position {x, y}
     * @returns {Array} Optimized and ordered patches
     */
    optimize(patches, startPoint = { x: 0, y: 0 }) {
        if (!patches || patches.length === 0) {
            console.log("FLSC: No patches to optimize");
            return [];
        }

        console.log(`\n=== FLSC OPTIMIZER START ===`);
        console.log(`Input: ${patches.length} patches`);

        // Step 1: Normalize all patches to standard format
        const normalized = this.normalizePatches(patches);

        // Step 2: Classify into Linear and Curved
        const { linear, curved } = this.classifyPatches(normalized);
        console.log(`Classified: ${linear.length} linear, ${curved.length} curved`);

        // Step 3: Optimize each group separately
        const optimizedLinear = this.optimizePath(linear, startPoint);

        // Get end point of linear group for connecting to curved
        const linearEnd = optimizedLinear.length > 0
            ? this.getEndPoint(optimizedLinear[optimizedLinear.length - 1])
            : startPoint;

        const optimizedCurved = this.optimizePath(curved, linearEnd);

        // Step 4: Assemble final trajectory (FLSC order)
        const result = [];

        // Add linear group
        result.push(...optimizedLinear);

        // Add transition from linear to curved (if both exist)
        if (optimizedLinear.length > 0 && optimizedCurved.length > 0) {
            const curvedStart = this.getStartPoint(optimizedCurved[0]);
            result.push({
                type: 'line',
                points: [
                    [linearEnd.x, linearEnd.y],
                    [curvedStart.x, curvedStart.y]
                ],
                data: { penup: true }
            });
        }

        // Add curved group
        result.push(...optimizedCurved);

        console.log(`Output: ${result.length} patches (${optimizedLinear.length} linear → ${optimizedCurved.length} curved)`);
        console.log(`=== FLSC OPTIMIZER END ===\n`);

        return result;
    }

    /**
     * Normalize patches to consistent format
     * Handles both text patches (already in meters) and drawing patches (Point objects)
     */
    normalizePatches(patches) {
        return patches.map(p => {
            if (!p) return null;

            // Already normalized (from text generation)
            if (p.points && Array.isArray(p.points[0])) {
                return p;
            }

            // Drawing patch with Point objects - needs conversion
            if (p.type === 'line' && p.data && p.data[0]?.actX !== undefined) {
                const p0 = p.data[0];
                const p1 = p.data[1];
                const penup = p.data[2];

                return {
                    type: 'line',
                    points: [[p0.actX, p0.actY], [p1.actX, p1.actY]],
                    data: { penup: penup || false }
                };
            }

            // Circle patch with Point objects
            if (p.type === 'circle' && p.data && p.data[0]?.actX !== undefined) {
                // We'll convert circles to line segments later, but mark as circle for classification
                return {
                    type: 'circle',
                    originalData: p.data, // Keep original for later processing
                    data: p.data // Also keep in data
                };
            }

            // Unknown format - log warning and skip
            console.warn("FLSC: Unknown patch format, skipping:", p);
            return null;
        }).filter(p => p !== null);
    }

    /**
     * Classify patches into linear and curved groups
     */
    classifyPatches(patches) {
        const linear = [];
        const curved = [];

        for (const patch of patches) {
            if (patch.type === 'line') {
                linear.push(patch);
            } else if (patch.type === 'circle') {
                curved.push(patch);
            } else {
                console.warn("FLSC: Unknown patch type:", patch.type);
            }
        }

        return { linear, curved };
    }

    /**
     * Optimize path order using Greedy Nearest Neighbor algorithm
     * Minimizes pen-up movements within a group
     */
    optimizePath(patches, startPoint) {
        if (patches.length === 0) return [];
        if (patches.length === 1) return patches;

        const optimized = [];
        const remaining = [...patches];
        let currentPos = startPoint;

        while (remaining.length > 0) {
            // Find nearest trajectory
            let nearestIndex = 0;
            let minDistance = Infinity;

            for (let i = 0; i < remaining.length; i++) {
                const patch = remaining[i];
                const patchStart = this.getStartPoint(patch);
                const distance = this.distance(currentPos, patchStart);

                if (distance < minDistance) {
                    minDistance = distance;
                    nearestIndex = i;
                }
            }

            // Add nearest to optimized list
            const nearest = remaining.splice(nearestIndex, 1)[0];
            optimized.push(nearest);

            // Update current position
            currentPos = this.getEndPoint(nearest);
        }

        return optimized;
    }

    /**
     * Get start point of a patch
     */
    getStartPoint(patch) {
        if (patch.type === 'line') {
            const p = patch.points[0];
            return { x: p[0], y: p[1] };
        }

        if (patch.type === 'circle') {
            // For circles, calculate start point from center and angles
            const data = patch.data || patch.originalData;
            if (data && data[0]?.actX !== undefined) {
                const c = data[0];
                const r = data[1] * (this.settings.m_p || 0.000936); // Convert pixels to meters
                const theta0 = data[2];

                return {
                    x: c.actX + r * Math.cos(theta0),
                    y: c.actY + r * Math.sin(theta0)
                };
            }
        }

        console.warn("FLSC: Cannot determine start point for patch:", patch);
        return { x: 0, y: 0 };
    }

    /**
     * Get end point of a patch
     */
    getEndPoint(patch) {
        if (patch.type === 'line') {
            const p = patch.points[1];
            return { x: p[0], y: p[1] };
        }

        if (patch.type === 'circle') {
            // For circles, calculate end point from center and angles
            const data = patch.data || patch.originalData;
            if (data && data[0]?.actX !== undefined) {
                const c = data[0];
                const r = data[1] * (this.settings.m_p || 0.000936);
                const theta1 = data[3]; // End angle

                return {
                    x: c.actX + r * Math.cos(theta1),
                    y: c.actY + r * Math.sin(theta1)
                };
            }
        }

        console.warn("FLSC: Cannot determine end point for patch:", patch);
        return { x: 0, y: 0 };
    }

    /**
     * Calculate Euclidean distance between two points
     */
    distance(p1, p2) {
        const dx = p2.x - p1.x;
        const dy = p2.y - p1.y;
        return Math.sqrt(dx * dx + dy * dy);
    }
}
