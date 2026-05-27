import math

def apply_curved_transform(trajectory, radius, start_angle_deg=90):
    """
    Maps a list of points (x, y, z) from a linear domain to a curved domain.
    
    Logic:
    - Input X becomes the arc length along the circle.
    - Input Y becomes a radial offset from the base radius.
    - Input Z remains unchanged (pen up/down).
    
    Args:
        trajectory: List of dictionaries {'x': float, 'y': float, 'z': float}
        radius: The base radius of the arc (where y=0 maps to).
        start_angle_deg: The starting angle of the text center or beginning (in degrees).
                         Usually 90 degrees corresponds to 'up' in standard math, 
                         but might need adjustment based on robot frame.
                         
    Returns:
        List of transformed dictionaries {'x': float, 'y': float, 'z': float}
    """
    transformed = []
    
    # We treat the text as being centered around x=0 or starting at x=0? 
    # Usually stick fonts generate text starting at x=0.
    # Let's assume x is the distance along the arc.
    # Angle theta = x / radius (in radians)
    
    # Convert start angle to radians
    start_angle_rad = math.radians(start_angle_deg)
    
    for point in trajectory:
        x_lin = point['x']
        y_lin = point['y']
        z = point['z']
        
        # Calculate the actual radius for this point
        # Y in linear text maps to radial distance.
        # If we want text to read "outward" (bottom of letters at inner radius),
        # we can add y_lin to radius.
        current_r = radius + y_lin
        
        # Calculate angle deviation from the start angle
        # Arc length s = r * theta  => theta = s / r
        # We use the base radius for angular calculation to keep characters "vertical" relative to center?
        # Or should we use current_r? Using base radius 'radius' ensures vertical lines in letters
        # become radial lines (converging to center).
        angle_offset = x_lin / radius
        
        # Final angle
        # We subtract angle_offset to write clockwise? or add for counter-clockwise?
        # Standard: +x is right. +Angle is CCW.
        # If we want text to flow left-to-right along the arc:
        # If we start at 90 deg (top), writing "Hello" (x increases):
        # We probably want to go CW (decreasing angle) so it reads left-to-right?
        # Or CCW?
        # Let's assume standard math for now: theta = start - (x / radius) for CW writing (Left to Right on top of arc)
        # Check user sketch... "Curved Workspace" shows a donut sector. Text usually flows along it.
        # Let's try: theta = start_angle_rad - angle_offset
        theta = start_angle_rad - angle_offset
        
        # Polar to Cartesian
        x_curved = current_r * math.cos(theta)
        y_curved = current_r * math.sin(theta)
        
        transformed.append({
            'x': x_curved,
            'y': y_curved,
            'z': z
        })
        
    return transformed
