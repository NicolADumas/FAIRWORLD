
"""
Simple Vector Font Generator for Robot Arm
Defines a "stick font" where each character is a set of lines/arcs.
Coordinates are normalized 0.0 to 1.0 within the character box.
"""

# Character definitions: List of strokes. 
# Each stroke is a list of points [(x,y), (x,y), ...].
# 'pen_up' is implicit between strokes.

"""
Vector Font Generator with Geometric Primitives
Supports 'line' (list of points) and 'ellipse' (parametric).
"""

import math

# Characters that should use the slower "curve" profile
CURVED_CHARS = {'B', 'C', 'D', 'G', 'J', 'O', 'P', 'Q', 'R', 'S', 'U', '0', '2', '3', '5', '6', '8', '9', '?', '&', '@', '(', ')', '{', '}', "'", '"', '$', '%'}

# Character definitions
# Format: List of Primitives.
# Primitive: 
#   {'type': 'line', 'points': [(x,y), ...]}
#   {'type': 'ellipse', 'center': (cx, cy), 'radii': (rx, ry), 'arc': (start_deg, end_deg)}
#   {'type': 'ellipse_path', 'points': [...]} # Implicit path of ellipses? No, keep simple.

FONT_DEFS = {
    # Straight Letters
    'A': [{'type': 'line', 'points': [(0,0), (0.5,1), (1,0)]}, 
          {'type': 'line', 'points': [(0.2, 0.4), (0.8, 0.4)]}],
    'E': [{'type': 'line', 'points': [(1,0), (0,0), (0,1), (1,1)]}, 
          {'type': 'line', 'points': [(0,0.5), (0.8,0.5)]}],
    'F': [{'type': 'line', 'points': [(0,0), (0,1), (1,1)]}, 
          {'type': 'line', 'points': [(0,0.5), (0.8,0.5)]}],
    'H': [{'type': 'line', 'points': [(0,0), (0,1)]}, 
          {'type': 'line', 'points': [(1,0), (1,1)]}, 
          {'type': 'line', 'points': [(0,0.5), (1,0.5)]}],
    'I': [{'type': 'line', 'points': [(0.5,0), (0.5,1)]}, 
          {'type': 'line', 'points': [(0,1), (1,1)]}, 
          {'type': 'line', 'points': [(0,0), (1,0)]}],
    'K': [{'type': 'line', 'points': [(0,0), (0,1)]}, 
          {'type': 'line', 'points': [(1,1), (0,0.5), (1,0)]}],
    'L': [{'type': 'line', 'points': [(0,1), (0,0), (1,0)]}],
    'M': [{'type': 'line', 'points': [(0,0), (0,1), (0.5,0.5), (1,1), (1,0)]}],
    'N': [{'type': 'line', 'points': [(0,0), (0,1), (1,0), (1,1)]}],
    'T': [{'type': 'line', 'points': [(0.5,0), (0.5,1)]}, 
          {'type': 'line', 'points': [(0,1), (1,1)]}],
    'V': [{'type': 'line', 'points': [(0,1), (0.5,0), (1,1)]}],
    'W': [{'type': 'line', 'points': [(0,1), (0.2,0), (0.5,0.5), (0.8,0), (1,1)]}],
    'X': [{'type': 'line', 'points': [(0,0), (1,1)]}, 
          {'type': 'line', 'points': [(0,1), (1,0)]}],
    'Y': [{'type': 'line', 'points': [(0,1), (0.5,0.5)]}, 
          {'type': 'line', 'points': [(1,1), (0.5,0.5), (0.5,0)]}],
    'Z': [{'type': 'line', 'points': [(0,1), (1,1), (0,0), (1,0)]}],

    # Curved & Refined Letters
    'B': [{'type': 'line', 'points': [(0,1), (0,0)]}, 
          {'type': 'line', 'points': [(0,1), (0.5,1)]}, 
          {'type': 'ellipse', 'center': (0.5, 0.75), 'radii': (0.4, 0.25), 'arc': (90, -90)},
          {'type': 'line', 'points': [(0.5,0.5), (0,0.5)]},
          {'type': 'line', 'points': [(0,0.5), (0.5,0.5)]}, 
          {'type': 'ellipse', 'center': (0.5, 0.25), 'radii': (0.4, 0.25), 'arc': (90, -90)},
          {'type': 'line', 'points': [(0.5,0), (0,0)]}],
          
    'C': [{'type': 'ellipse', 'center': (0.5, 0.5), 'radii': (0.5, 0.5), 'arc': (45, 315)}], 
    
    'D': [{'type': 'ellipse', 'center': (0.4, 0.5), 'radii': (0.6, 0.5), 'arc': (-90, 90)}, 
          {'type': 'line', 'points': [(0.4, 1), (0, 1), (0, 0), (0.4, 0)]}],
          
    'G': [{'type': 'ellipse', 'center': (0.5, 0.5), 'radii': (0.5, 0.5), 'arc': (45, 315)}, 
          {'type': 'line', 'points': [(0.85, 0.15), (0.85, 0.4), (0.5, 0.4)]}],

    'J': [{'type': 'line', 'points': [(0.8, 1), (0.8, 0.3)]},
          {'type': 'ellipse', 'center': (0.4, 0.3), 'radii': (0.4, 0.3), 'arc': (0, -180)}],

    'O': [{'type': 'ellipse', 'center': (0.5, 0.5), 'radii': (0.5, 0.5), 'arc': (0, -360)}],
    
    'P': [{'type': 'line', 'points': [(0,0), (0,1)]}, 
          {'type': 'line', 'points': [(0,1), (0.5,1)]},
          {'type': 'ellipse', 'center': (0.5, 0.75), 'radii': (0.4, 0.25), 'arc': (90, -90)},
          {'type': 'line', 'points': [(0.5,0.5), (0,0.5)]}],
    
    'Q': [{'type': 'ellipse', 'center': (0.5, 0.5), 'radii': (0.5, 0.5), 'arc': (0, 360)},
          {'type': 'line', 'points': [(0.6, 0.2), (1, 0)]}],

    'R': [{'type': 'line', 'points': [(0,0), (0,1)]}, 
          {'type': 'line', 'points': [(0,1), (0.5,1)]},
          {'type': 'ellipse', 'center': (0.5, 0.75), 'radii': (0.4, 0.25), 'arc': (90, -90)},
          {'type': 'line', 'points': [(0.5,0.5), (0,0.5)]},
          {'type': 'line', 'points': [(0.4, 0.5), (1, 0)]}], 

    'S': [{'type': 'ellipse', 'center': (0.5, 0.75), 'radii': (0.5, 0.25), 'arc': (25, 270)}, # Extended Top
          {'type': 'ellipse', 'center': (0.5, 0.25), 'radii': (0.5, 0.25), 'arc': (90, -155)}], # Extended Bottom

    'U': [{'type': 'line', 'points': [(0, 1), (0, 0.3)]},
          {'type': 'ellipse', 'center': (0.5, 0.3), 'radii': (0.5, 0.3), 'arc': (180, 360)}, 
          {'type': 'line', 'points': [(1, 0.3), (1, 1)]}],

    # Numbers
    '0': [{'type': 'ellipse', 'center': (0.5, 0.5), 'radii': (0.45, 0.5), 'arc': (0, 360)}],
    '1': [{'type': 'line', 'points': [(0.3, 0.7), (0.5, 1), (0.5, 0)]}, 
          {'type': 'line', 'points': [(0.2, 0), (0.8, 0)]}],
    '2': [{'type': 'ellipse', 'center': (0.5, 0.7), 'radii': (0.5, 0.3), 'arc': (160, -50)}, # End at -50 deg -> x=0.82, y=0.47
          {'type': 'line', 'points': [(0.82, 0.47), (0, 0), (1, 0)]}], # Connected exactly
    '3': [{'type': 'ellipse', 'center': (0.5, 0.75), 'radii': (0.4, 0.25), 'arc': (210, -90)}, # Ends at (0.5, 0.5)
          {'type': 'ellipse', 'center': (0.5, 0.25), 'radii': (0.45, 0.25), 'arc': (90, -210)}], # Starts at (0.5, 0.5)
          
    '4': [{'type': 'line', 'points': [(0.7,0), (0.7,1)]}, 
          {'type': 'line', 'points': [(0,1), (0,0.4), (1,0.4)]}], 
    '5': [{'type': 'line', 'points': [(0.9,1), (0.1,1), (0.1,0.55)]}, 
          {'type': 'ellipse', 'center': (0.5, 0.35), 'radii': (0.48, 0.35), 'arc': (146, -135)}], # Extended more
    '6': [{'type': 'line', 'points': [(0.8, 1.0), (0.1, 0.45)]}, # Extended top stalk
          {'type': 'ellipse', 'center': (0.5, 0.3), 'radii': (0.5, 0.3), 'arc': (0, 360)}], 
    '7': [{'type': 'line', 'points': [(0,1), (1,1), (0.4,0)]}], 
    '8': [{'type': 'ellipse', 'center': (0.5, 0.75), 'radii': (0.4, 0.25), 'arc': (0, 360)}, 
          {'type': 'ellipse', 'center': (0.5, 0.25), 'radii': (0.5, 0.25), 'arc': (0, 360)}],
    '9': [{'type': 'ellipse', 'center': (0.5, 0.7), 'radii': (0.45, 0.3), 'arc': (0, 360)},
          {'type': 'line', 'points': [(0.95, 0.7), (0.95, 0.5)]},
          {'type': 'ellipse', 'center': (0.5, 0.5), 'radii': (0.45, 0.5), 'arc': (0, -110)}], # Extended tail
          
    ' ': [],
    '-': [{'type': 'line', 'points': [(0.2, 0.5), (0.8, 0.5)]}],
    '_': [{'type': 'line', 'points': [(0, 0), (1, 0)]}],
    '.': [{'type': 'ellipse', 'center': (0.5, 0.1), 'radii': (0.05, 0.05), 'arc': (0, 360)}], # Small Circle Dot
    ',': [{'type': 'ellipse', 'center': (0.5, 0.1), 'radii': (0.05, 0.05), 'arc': (0, 360)}, # Dot
          {'type': 'line', 'points': [(0.5, 0.05), (0.4, -0.1)]}], # Tail
          
    ':': [{'type': 'ellipse', 'center': (0.5, 0.1), 'radii': (0.05, 0.05), 'arc': (0, 360)}, # Bottom dot
          {'type': 'ellipse', 'center': (0.5, 0.6), 'radii': (0.05, 0.05), 'arc': (0, 360)}], # Top dot
          
    ';': [{'type': 'ellipse', 'center': (0.5, 0.1), 'radii': (0.05, 0.05), 'arc': (0, 360)}, # Bottom dot
          {'type': 'line', 'points': [(0.5, 0.05), (0.4, -0.1)]}, # Tail
          {'type': 'ellipse', 'center': (0.5, 0.6), 'radii': (0.05, 0.05), 'arc': (0, 360)}], # Top dot

    "'": [{'type': 'line', 'points': [(0.5, 1.0), (0.5, 0.7)]}], # Straight tick
    '"': [{'type': 'line', 'points': [(0.35, 1.0), (0.35, 0.7)]},
          {'type': 'line', 'points': [(0.65, 1.0), (0.65, 0.7)]}], # Double straight tick

    '!': [{'type': 'line', 'points': [(0.5, 1), (0.5, 0.35)]}, # Shortened line
          {'type': 'ellipse', 'center': (0.5, 0.1), 'radii': (0.05, 0.05), 'arc': (0, 360)}], # Dot with gap
          
    '?': [{'type': 'ellipse', 'center': (0.5, 0.7), 'radii': (0.3, 0.3), 'arc': (180, -45)}, # Hook
          {'type': 'line', 'points': [(0.71, 0.48), (0.5, 0.35)]}, # Vertical part shorter
          {'type': 'ellipse', 'center': (0.5, 0.1), 'radii': (0.05, 0.05), 'arc': (0, 360)}], # Dot with gap
    
    # Brackets
    '(': [{'type': 'ellipse', 'center': (0.7, 0.5), 'radii': (0.3, 0.6), 'arc': (110, 250)}],
    ')': [{'type': 'ellipse', 'center': (0.3, 0.5), 'radii': (0.3, 0.6), 'arc': (70, -70)}],
    '[': [{'type': 'line', 'points': [(0.7, 1), (0.3, 1), (0.3, 0), (0.7, 0)]}],
    ']': [{'type': 'line', 'points': [(0.3, 1), (0.7, 1), (0.7, 0), (0.3, 0)]}],
    '{': [{'type': 'ellipse', 'center': (0.7, 0.75), 'radii': (0.4, 0.25), 'arc': (90, 210)}, # Top Hook In
          {'type': 'line', 'points': [(0.354, 0.625), (0.2, 0.5)]}, # Beak In Line
          {'type': 'line', 'points': [(0.2, 0.5), (0.354, 0.375)]}, # Beak Out Line
          {'type': 'ellipse', 'center': (0.7, 0.25), 'radii': (0.4, 0.25), 'arc': (150, 270)}], # Bottom Hook Out
          
    '}': [{'type': 'ellipse', 'center': (0.3, 0.75), 'radii': (0.4, 0.25), 'arc': (90, -30)}, # Top Hook In
          {'type': 'line', 'points': [(0.646, 0.625), (0.8, 0.5)]}, # Beak In Line
          {'type': 'line', 'points': [(0.8, 0.5), (0.646, 0.375)]}, # Beak Out Line
          {'type': 'ellipse', 'center': (0.3, 0.25), 'radii': (0.4, 0.25), 'arc': (30, -90)}], # Bottom Hook Out
    
    # Symbols
    '*': [{'type': 'line', 'points': [(0.5, 0.8), (0.5, 0.2)]}, # Vertical
          {'type': 'line', 'points': [(0.2, 0.7), (0.8, 0.3)]}, # Diagonal \
          {'type': 'line', 'points': [(0.8, 0.7), (0.2, 0.3)]}], # Diagonal /
          
    '#': [{'type': 'line', 'points': [(0.2, 0.6), (0.8, 0.6)]}, # Horizontal Top
          {'type': 'line', 'points': [(0.2, 0.4), (0.8, 0.4)]}, # Horizontal Bottom
          {'type': 'line', 'points': [(0.35, 0.15), (0.45, 0.85)]}, # Vertical Left (Slanted)
          {'type': 'line', 'points': [(0.55, 0.15), (0.65, 0.85)]}], # Vertical Right (Slanted)
          
    '+': [{'type': 'line', 'points': [(0.1, 0.5), (0.9, 0.5)]},
          {'type': 'line', 'points': [(0.5, 0.1), (0.5, 0.9)]}],
    '=': [{'type': 'line', 'points': [(0.1, 0.4), (0.9, 0.4)]},
          {'type': 'line', 'points': [(0.1, 0.6), (0.9, 0.6)]}],
    '<': [{'type': 'line', 'points': [(0.8, 1), (0.2, 0.5), (0.8, 0)]}],
    '>': [{'type': 'line', 'points': [(0.2, 1), (0.8, 0.5), (0.2, 0)]}],
    '/': [{'type': 'line', 'points': [(0.2, 0), (0.8, 1)]}],
    '\\': [{'type': 'line', 'points': [(0.2, 1), (0.8, 0)]}], # Escaped backslash
    '|': [{'type': 'line', 'points': [(0.5, 0), (0.5, 1)]}],
    
    '@': [{'type': 'ellipse', 'center': (0.5, 0.4), 'radii': (0.2, 0.2), 'arc': (0, 360)}, # Inner 'a' circle
          {'type': 'line', 'points': [(0.7, 0.6), (0.7, 0.154)]}, # Inner 'a' vertical - Extended to meet spiral at -60deg
          {'type': 'ellipse', 'center': (0.5, 0.5), 'radii': (0.4, 0.4), 'arc': (-60, 270)}], # Spiral starts EXACTLY at line end
          
    '&': [{'type': 'line', 'points': [(0.9, 0.05), (0.375, 0.48)]}, # 1. Diagonal Up
          {'type': 'ellipse', 'center': (0.5, 0.7), 'radii': (0.25, 0.25), 'arc': (240, -60)}, # 2. Top Loop
          {'type': 'line', 'points': [(0.625, 0.48), (0.375, 0.416)]}, # 3. Diagonal Down (Smoother connection at 120 deg)
          {'type': 'ellipse', 'center': (0.55, 0.2), 'radii': (0.35, 0.25), 'arc': (120, 360)}], # 4. Bottom Cup (Starts high for tangent)
          
    '$': [{'type': 'line', 'points': [(0.5, 1.1), (0.5, -0.1)]}, # Vertical Line (Extend slightly)
          {'type': 'ellipse', 'center': (0.5, 0.75), 'radii': (0.35, 0.25), 'arc': (25, 270)}, # Top S
          {'type': 'ellipse', 'center': (0.5, 0.25), 'radii': (0.4, 0.25), 'arc': (90, -155)}], # Bottom S
          
    '%': [{'type': 'line', 'points': [(0.2, 0), (0.8, 1)]}, # Diagonal Stroke
          {'type': 'ellipse', 'center': (0.3, 0.8), 'radii': (0.1, 0.1), 'arc': (0, 360)}, # Top-Left Circle
          {'type': 'ellipse', 'center': (0.7, 0.2), 'radii': (0.1, 0.1), 'arc': (0, 360)}], # Bottom-Right Circle
          
    # Accented Characters (Reuse base strokes + add accent)
    'À': [{'type': 'line', 'points': [(0,0), (0.5,1), (1,0)]}, 
          {'type': 'line', 'points': [(0.2, 0.4), (0.8, 0.4)]},
          {'type': 'line', 'points': [(0.3, 1.4), (0.7, 1.25)]}], # Raised Grave
          
    'È': [{'type': 'line', 'points': [(1,0), (0,0), (0,1), (1,1)]}, 
          {'type': 'line', 'points': [(0,0.5), (0.8,0.5)]},
          {'type': 'line', 'points': [(0.3, 1.4), (0.7, 1.25)]}], # Raised Grave
          
    'É': [{'type': 'line', 'points': [(1,0), (0,0), (0,1), (1,1)]}, 
          {'type': 'line', 'points': [(0,0.5), (0.8,0.5)]},
          {'type': 'line', 'points': [(0.3, 1.25), (0.7, 1.4)]}], # Raised Acute
          
    'Ì': [{'type': 'line', 'points': [(0.5,0), (0.5,1)]}, 
          {'type': 'line', 'points': [(0,1), (1,1)]}, 
          {'type': 'line', 'points': [(0,0), (1,0)]},
          {'type': 'line', 'points': [(0.3, 1.4), (0.7, 1.25)]}], # Raised Grave
          
    'Ò': [{'type': 'ellipse', 'center': (0.5, 0.5), 'radii': (0.5, 0.5), 'arc': (0, -360)},
          {'type': 'line', 'points': [(0.3, 1.4), (0.7, 1.25)]}], # Raised Grave
          
    'Ù': [{'type': 'line', 'points': [(0, 1), (0, 0.3)]},
          {'type': 'ellipse', 'center': (0.5, 0.3), 'radii': (0.5, 0.3), 'arc': (180, 360)}, 
          {'type': 'line', 'points': [(1, 0.3), (1, 1)]},
          {'type': 'line', 'points': [(0.3, 1.4), (0.7, 1.25)]}], # Raised Grave
          
    # Extra Symbols
    '^': [{'type': 'line', 'points': [(0.2, 0.6), (0.5, 1)]},
          {'type': 'line', 'points': [(0.5, 1), (0.8, 0.6)]}],
          
    '°': [{'type': 'ellipse', 'center': (0.2, 0.9), 'radii': (0.15, 0.15), 'arc': (0, 360)}],
    
    '€': [{'type': 'ellipse', 'center': (0.55, 0.5), 'radii': (0.4, 0.5), 'arc': (30, 330)}, # C-shape (shifted right)
          {'type': 'line', 'points': [(0.0, 0.6), (0.7, 0.6)]}, # Top Line (Crosses left)
          {'type': 'line', 'points': [(0.0, 0.4), (0.7, 0.4)]}], # Bottom Line (Crosses left)


    
    '£': [{'type': 'ellipse', 'center': (0.5, 0.75), 'radii': (0.35, 0.25), 'arc': (30, 210)}, # Top hook
          {'type': 'line', 'points': [(0.2, 0.65), (0.2, 0.2)]}, # Vertical down
          {'type': 'line', 'points': [(0.2, 0.2), (0.8, 0.2)]}, # Horizontal base
          {'type': 'line', 'points': [(0.1, 0.5), (0.6, 0.5)]}], # Cross bar

    # --- ARROWS ---
    '←': [{'type': 'line', 'points': [(0.8, 0.5), (0.2, 0.5)]}, 
          {'type': 'line', 'points': [(0.4, 0.7), (0.2, 0.5), (0.4, 0.3)]}],
    '↑': [{'type': 'line', 'points': [(0.5, 0.2), (0.5, 0.8)]}, 
          {'type': 'line', 'points': [(0.3, 0.6), (0.5, 0.8), (0.7, 0.6)]}],
    '→': [{'type': 'line', 'points': [(0.2, 0.5), (0.8, 0.5)]}, 
          {'type': 'line', 'points': [(0.6, 0.7), (0.8, 0.5), (0.6, 0.3)]}],
    '↓': [{'type': 'line', 'points': [(0.5, 0.8), (0.5, 0.2)]}, 
          {'type': 'line', 'points': [(0.3, 0.4), (0.5, 0.2), (0.7, 0.4)]}],
    '↔': [{'type': 'line', 'points': [(0.2, 0.5), (0.8, 0.5)]}, 
          {'type': 'line', 'points': [(0.3, 0.6), (0.1, 0.5), (0.3, 0.4)]},
          {'type': 'line', 'points': [(0.7, 0.6), (0.9, 0.5), (0.7, 0.4)]}],

    # --- WEATHER ---
    '☀': [{'type': 'ellipse', 'center': (0.5, 0.5), 'radii': (0.2, 0.2), 'arc': (0, 360)}, # Sun Body
          # Rays (8)
          {'type': 'line', 'points': [(0.5, 0.8), (0.5, 0.95)]}, 
          {'type': 'line', 'points': [(0.5, 0.2), (0.5, 0.05)]},
          {'type': 'line', 'points': [(0.8, 0.5), (0.95, 0.5)]},
          {'type': 'line', 'points': [(0.2, 0.5), (0.05, 0.5)]},
          {'type': 'line', 'points': [(0.71, 0.71), (0.82, 0.82)]},
          {'type': 'line', 'points': [(0.29, 0.29), (0.18, 0.18)]},
          {'type': 'line', 'points': [(0.29, 0.71), (0.18, 0.82)]},
          {'type': 'line', 'points': [(0.71, 0.29), (0.82, 0.18)]}],
          
    '☁': [{'type': 'line', 'points': [(0.2, 0.3), (0.8, 0.3)]}, # Bottom Flat
          {'type': 'ellipse', 'center': (0.2, 0.45), 'radii': (0.15, 0.15), 'arc': (90, 270)}, # Left arc
          {'type': 'ellipse', 'center': (0.4, 0.6), 'radii': (0.2, 0.2), 'arc': (0, 180)}, # Top mid big
          {'type': 'ellipse', 'center': (0.7, 0.5), 'radii': (0.15, 0.15), 'arc': (-45, 180)}, # Right top
          {'type': 'ellipse', 'center': (0.8, 0.45), 'radii': (0.15, 0.15), 'arc': (-90, 45)}], # Right bottom
          
    '⚡': [{'type': 'line', 'points': [(0.6, 1.0), (0.3, 0.5), (0.5, 0.5), (0.4, 0.0)]}], # Bolt

    # --- SUITS ---
    '♠': [{'type': 'ellipse', 'center': (0.35, 0.4), 'radii': (0.15, 0.15), 'arc': (0, 180)}, # Left lobe
          {'type': 'ellipse', 'center': (0.65, 0.4), 'radii': (0.15, 0.15), 'arc': (0, 180)}, # Right lobe
          {'type': 'line', 'points': [(0.2, 0.4), (0.5, 0.9), (0.8, 0.4)]}, # Top Spike
          {'type': 'line', 'points': [(0.5, 0.4), (0.5, 0.1), (0.3, 0.0), (0.7, 0.0), (0.5, 0.1)]}], # Base

    '♦': [{'type': 'line', 'points': [(0.5, 0.1), (0.2, 0.5), (0.5, 0.9), (0.8, 0.5), (0.5, 0.1)]}, # Outer
          {'type': 'line', 'points': [(0.5, 0.3), (0.35, 0.5), (0.5, 0.7), (0.65, 0.5), (0.5, 0.3)]}], # Inner (to look filled/different)

    '♣': [{'type': 'ellipse', 'center': (0.5, 0.7), 'radii': (0.15, 0.15), 'arc': (0, 360)}, # Top Leaf

          {'type': 'ellipse', 'center': (0.3, 0.45), 'radii': (0.15, 0.15), 'arc': (0, 360)}, # Left Leaf
          {'type': 'ellipse', 'center': (0.7, 0.45), 'radii': (0.15, 0.15), 'arc': (0, 360)}, # Right Leaf
          {'type': 'line', 'points': [(0.5, 0.45), (0.5, 0.1), (0.3, 0.0), (0.7, 0.0), (0.5, 0.1)]}], # Base

    # --- OFFICE / TECH ---
    '✉': [{'type': 'line', 'points': [(0.1, 0.2), (0.1, 0.8), (0.9, 0.8), (0.9, 0.2), (0.1, 0.2)]}, # Envelope Box
          {'type': 'line', 'points': [(0.1, 0.8), (0.5, 0.5), (0.9, 0.8)]}], # Flap
    
    '⚓': [{'type': 'line', 'points': [(0.5, 0.8), (0.5, 0.2)]}, # Shank
          {'type': 'line', 'points': [(0.3, 0.7), (0.7, 0.7)]}, # Stock
          {'type': 'ellipse', 'center': (0.5, 0.9), 'radii': (0.1, 0.1), 'arc': (0, 360)}, # Ring
          {'type': 'ellipse', 'center': (0.5, 0.4), 'radii': (0.3, 0.3), 'arc': (180, 360)}, # Bowl
          {'type': 'line', 'points': [(0.2, 0.4), (0.25, 0.5)]}, # Fluke L
          {'type': 'line', 'points': [(0.8, 0.4), (0.75, 0.5)]}], # Fluke R



    # --- SHAPES ---
    '□': [{'type': 'line', 'points': [(0.2, 0.2), (0.2, 0.8), (0.8, 0.8), (0.8, 0.2), (0.2, 0.2)]}], # Square
    '△': [{'type': 'line', 'points': [(0.2, 0.2), (0.5, 0.8), (0.8, 0.2), (0.2, 0.2)]}], # Triangle
    '○': [{'type': 'ellipse', 'center': (0.5, 0.5), 'radii': (0.35, 0.35), 'arc': (0, 360)}], # Circle
    '◊': [{'type': 'line', 'points': [(0.5, 0.1), (0.2, 0.5), (0.5, 0.9), (0.8, 0.5), (0.5, 0.1)]}], # Diamond

    # --- MATH ---
    '±': [{'type': 'line', 'points': [(0.2, 0.7), (0.8, 0.7)]}, # Plus H
          {'type': 'line', 'points': [(0.5, 0.5), (0.5, 0.9)]}, # Plus V
          {'type': 'line', 'points': [(0.2, 0.3), (0.8, 0.3)]}], # Minus
    '×': [{'type': 'line', 'points': [(0.3, 0.3), (0.7, 0.7)]}, 
          {'type': 'line', 'points': [(0.3, 0.7), (0.7, 0.3)]}],
    '÷': [{'type': 'line', 'points': [(0.2, 0.5), (0.8, 0.5)]}, 
          {'type': 'ellipse', 'center': (0.5, 0.75), 'radii': (0.05, 0.05), 'arc': (0, 360)},
          {'type': 'ellipse', 'center': (0.5, 0.25), 'radii': (0.05, 0.05), 'arc': (0, 360)}],
    '≠': [{'type': 'line', 'points': [(0.2, 0.4), (0.8, 0.4)]},
          {'type': 'line', 'points': [(0.2, 0.6), (0.8, 0.6)]},
          {'type': 'line', 'points': [(0.3, 0.2), (0.7, 0.8)]}], # Slash
    '√': [{'type': 'line', 'points': [(0.1, 0.5), (0.3, 0.2), (0.4, 0.9), (0.9, 0.9)]}],

    # --- MISC ---
    '♥': [{'type': 'ellipse', 'center': (0.3, 0.7), 'radii': (0.2, 0.2), 'arc': (0, 180)}, # Top Left
          {'type': 'ellipse', 'center': (0.7, 0.7), 'radii': (0.2, 0.2), 'arc': (0, 180)}, # Top Right
          {'type': 'line', 'points': [(0.1, 0.7), (0.5, 0.1)]}, # Left Down
          {'type': 'line', 'points': [(0.9, 0.7), (0.5, 0.1)]}], # Right Down
    '★': [{'type': 'line', 'points': [(0.5, 0.9), (0.65, 0.4), (0.95, 0.4), (0.7, 0.2), 
                                     (0.8, -0.3), (0.5, 0.0), (0.2, -0.3), (0.3, 0.2), 
                                     (0.05, 0.4), (0.35, 0.4), (0.5, 0.9)]}], # Star (Polyline)
    '☺': [{'type': 'ellipse', 'center': (0.5, 0.5), 'radii': (0.45, 0.45), 'arc': (0, 360)}, # Face
          {'type': 'ellipse', 'center': (0.35, 0.65), 'radii': (0.05, 0.05), 'arc': (0, 360)}, # Eye L
          {'type': 'ellipse', 'center': (0.65, 0.65), 'radii': (0.05, 0.05), 'arc': (0, 360)}, # Eye R
          {'type': 'ellipse', 'center': (0.5, 0.5), 'radii': (0.25, 0.2), 'arc': (200, 340)}], # Smile
    '♪': [{'type': 'ellipse', 'center': (0.2, 0.2), 'radii': (0.1, 0.08), 'arc': (0, 360)}, # Note Head
          {'type': 'line', 'points': [(0.3, 0.2), (0.3, 0.8), (0.5, 0.8)]}], # Stem + Flag
}



def get_char_strokes(char):
    return FONT_DEFS.get(char.upper(), [])

def sample_ellipse(center, radii, arc, steps=None):
    """
    Samples an elliptical arc.
    center: (cx, cy)
    radii: (rx, ry)
    arc: (start_deg, end_deg)
    """
    cx, cy = center
    rx, ry = radii
    start_rad = math.radians(arc[0])
    end_rad = math.radians(arc[1])
    
    # Adaptive resolution if steps not provided
    # Aim for ~2 degrees per step for high smoothness (Ultra Quality)
    if steps is None:
        span_deg = abs(arc[1] - arc[0])
        steps = max(40, int(span_deg / 1.0)) # ULTRA SMOOTH: Every 1 degree
    
    points = []
    
    # Determine direction
    # We want to go from start to end.
    span = end_rad - start_rad
    step_rad = span / steps
    
    for i in range(steps + 1):
        theta = start_rad + i * step_rad
        x = cx + rx * math.cos(theta)
        y = cy + ry * math.sin(theta)
        points.append((x, y))
        
    return points

def text_to_traj(text: str, start_pos: tuple, font_size: float, char_spacing: float):
    """
    Generates a list of patches for the given text.
    Groups contiguous segments into 'polyline' patches for smoother execution.
    """
    traj_patches = []
    cursor_x, cursor_y = start_pos
    
    pending_points = []

    def flush_pending():
        nonlocal pending_points
        if not pending_points: return

        # If only one point (shouldn't happen with correct logic, but safety),
        # cannot make a polyline.
        if len(pending_points) < 2:
            pending_points = []
            return
            
        # Determine profile based on character type
        # Ideally we know which character we are processing. 
        # But 'pending_points' could theoretically span characters if we didn't force flush on space/newline?
        # The logic below flushes on space/newline, so pending_points usually belongs to one word or segment.
        # But wait, we iterate char by char.
        # We need to know if the CURRENT pending points belong to a curved char.
        # We can track the "most recent character" or just assign 'curve' if ANY char in the buffer was curved?
        # Actually, `text_to_traj` iterates chars. We should probably track the `current_profile` property.
        
        patch_profile = 'curve' if is_curved_sequence else 'linear'

        patch = {
            'type': 'polyline',
            'points': pending_points,
            'data': {'penup': False, 'profile': patch_profile}
        }
        traj_patches.append(patch)
        pending_points = []
        # Reset curve flag after flush? Or is it handled by char loop?
        # It's better to flush when profile changes.

    is_curved_sequence = False # Track if current pending sequence contains curved shapes

    for char in text:
        if char == '\n':
            flush_pending()
            cursor_x = start_pos[0]
            cursor_y -= font_size * 1.5 
            continue

        if char == ' ':
            flush_pending()
            cursor_x += (font_size * 0.8) + char_spacing
            continue

        # Check if we need to switch profile
        char_is_curved = char.upper() in CURVED_CHARS
        if pending_points and char_is_curved != is_curved_sequence:
             # Profile mismatch, flush previous
             flush_pending()
        
        is_curved_sequence = char_is_curved

        primitives = get_char_strokes(char)
        char_width = font_size
        
        for prim in primitives:
            # 1. Get Normalized Points
            norm_points = []
            
            if prim['type'] == 'line':
                norm_points = prim['points']
            elif prim['type'] == 'ellipse':
                norm_points = sample_ellipse(prim['center'], prim['radii'], prim['arc'])
                
            if not norm_points: continue
            
            # 2. Scale & Translate to World
            world_points = []
            for p in norm_points:
                wx = cursor_x + p[0] * char_width * 0.8 
                wy = cursor_y + p[1] * font_size
                world_points.append((wx, wy))
            
            # 3. Accumulate Points for Polyline
            start_pt = world_points[0]
            
            # Check continuity with pending points
            if pending_points:
                last_pt = pending_points[-1]
                dist = ((last_pt[0]-start_pt[0])**2 + (last_pt[1]-start_pt[1])**2)**0.5
                
                if dist > 0.001:
                    # Discontinuity -> End current polyline
                    flush_pending()
                    
                    # Add PENUP from previous patch end to new start
                    if traj_patches:
                        prev_end = traj_patches[-1]['points'][-1]
                        traj_patches.append({
                            'type': 'line',
                            'points': [prev_end, start_pt],
                            'data': {'penup': True, 'profile': 'jump'}
                        })
                    pending_points.append(start_pt)
                else:
                    # Continuous - CHECK FOR SHARP CORNER
                    # We have pending_points (last_pt) and world_points (start_pt is 0, next is 1)
                    # We need the vector ending at last_pt (v_in) and vector starting at start_pt (v_out)
                    
                    if len(pending_points) >= 2 and len(world_points) >= 2:
                        p_prev = pending_points[-2]
                        p_curr = pending_points[-1] # == start_pt roughly
                        p_next = world_points[1] # Next point in new primitive
                        
                        v_in = (p_curr[0]-p_prev[0], p_curr[1]-p_prev[1])
                        v_out = (p_next[0]-p_curr[0], p_next[1]-p_curr[1])
                        
                        mag_in = math.sqrt(v_in[0]**2 + v_in[1]**2)
                        mag_out = math.sqrt(v_out[0]**2 + v_out[1]**2)
                        
                        if mag_in > 1e-6 and mag_out > 1e-6:
                            dot = v_in[0]*v_out[0] + v_in[1]*v_out[1]
                            similarity = dot / (mag_in * mag_out)
                            # similarity = cos(theta). 1=straight, 0=90deg turn, -1=180turn
                            # Threshold: if angle > 45 deg, split.
                            # 45 deg -> cos(45) = 0.707
                            # So if similarity < 0.707, it's a sharp turn.
                            if similarity < 0.707:
                                # Sharp Corner Detected!
                                flush_pending()
                                # We don't need a pen-up, just a split to force zero velocity stop.
                                pending_points.append(start_pt)
                            else:
                                # Smooth enough to continue
                                pass
                        else:
                             pass
                    
                    pass 
            else:
                 # No pending points. Check continuity with previous COMPLETED patch for PenUp
                 if traj_patches:
                     # Get last point of last patch
                     last_patch_points = traj_patches[-1]['points']
                     prev_end = last_patch_points[-1] # Works for polyline (list) or line (2-list)
                     
                     dist = ((prev_end[0]-start_pt[0])**2 + (prev_end[1]-start_pt[1])**2)**0.5
                     if dist > 0.001:
                         traj_patches.append({
                            'type': 'line',
                            'points': [prev_end, start_pt],
                            'data': {'penup': True, 'profile': 'jump'}
                        })
                 pending_points.append(start_pt)

            # Append new points
            # If we just connected continuously, start_pt might verify == last_pt. 
            # If so, skip it to avoid zero-length segment?
            # Or just append. Polyline logic handle 0 length? Better avoid.
            start_idx = 1 if (len(pending_points) > 1 and pending_points[-2] == start_pt) else 1
            # Actually, `pending_points` already has `start_pt` added in the logic above (either appended or new).
            # Wait, `pending_points.append(start_pt)` is inside the if/else blocks.
            # So `start_pt` is IN `pending_points`.
            # We need to extend `world_points[1:]`.
            
            # Append new points loop
            for i in range(1, len(world_points)):
                next_pt = world_points[i]
                
                # Check for sharp corner with previous segment
                if len(pending_points) >= 2:
                    p_prev = pending_points[-2]
                    p_curr = pending_points[-1]
                    p_next = next_pt
                    
                    v_in = (p_curr[0]-p_prev[0], p_curr[1]-p_prev[1])
                    v_out = (p_next[0]-p_curr[0], p_next[1]-p_curr[1])
                    
                    mag_in = math.sqrt(v_in[0]**2 + v_in[1]**2)
                    mag_out = math.sqrt(v_out[0]**2 + v_out[1]**2)
                    
                    if mag_in > 1e-6 and mag_out > 1e-6:
                        dot = v_in[0]*v_out[0] + v_in[1]*v_out[1]
                        similarity = dot / (mag_in * mag_out)
                        if similarity < 0.707: # 45 degrees
                            # Sharp corner!
                            flush_pending()
                            # Start new segment from the vertex
                            pending_points.append(p_curr)
                            
                pending_points.append(next_pt)

        cursor_x += (font_size * 0.8) + char_spacing

    # Flush any remaining
    flush_pending()

    return traj_patches
