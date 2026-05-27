import cv2
import numpy as np
import base64
import io
from PIL import Image
from svgpathtools import svg2paths, Path, Line, Arc, CubicBezier, QuadraticBezier
import tempfile
import os

def process_image(file_data_base64, options):
    """
    Process an image (Raster or SVG) and return a trajectory.
    
    Args:
        file_data_base64 (str): Base64 encoded file data suitable for data URI scheme (start with "data:image/...")
        options (dict): Configuration options
            - mode: 'raster' | 'svg'
            - width: float (target width in meters)
            - x: float (offset x in meters)
            - y: float (offset y in meters)
            - rotation: float (degrees)
            - threshold: int (canny threshold for raster)
            - inverted: bool (for raster)
            
    Returns:
        list: List of patches (lines/curves) compatible with the robot GUI format.
    """
    
    try:
        # Decode Base64
        if ',' in file_data_base64:
            header, encoded = file_data_base64.split(',', 1)
        else:
            encoded = file_data_base64
            
        decoded_data = base64.b64decode(encoded)
        
        mode = options.get('mode', 'raster')
        width_m = float(options.get('width', 0.10)) # Target width in meters
        
        raw_paths = [] # List of lists of points [[(x,y), (x,y)], ...]
        
        import time
        t_start = time.time()
        
        # Use provided logger or default to print
        log = options.get('logger', print)
        log(f"[DEBUG] Starting Image Processing. Mode: {mode}")
        
        if mode == 'svg':
            raw_paths = _process_svg(decoded_data)
        elif mode == 'vector_bw' or True: # Default to new Vector BW engine for now
            # Check stop before heavy operation
            if options.get('check_stop') and options['check_stop']():
                log("[DEBUG] Processing Aborted by User.")
                return []
            raw_paths = _process_vector_bw(decoded_data, options, log)
        else:
            raw_paths = _process_raster(decoded_data, options)
            
        t_proc = time.time()
        log(f"[DEBUG] Processing complete in {t_proc - t_start:.4f}s. Found {len(raw_paths)} raw paths.")
            
        if options.get('check_stop') and options['check_stop']():
            log("[DEBUG] Processing Aborted by User.")
            return []
            
        if not raw_paths:
            log("[DEBUG] No paths found.")
            return []
            
        # --- Normalization & Scaling ---
        
        # 1. Find Bounding Box of raw paths
        all_points = [p for path in raw_paths for p in path]
        if not all_points: 
            return []
            
        min_x = min(p[0] for p in all_points)
        max_x = max(p[0] for p in all_points)
        min_y = min(p[1] for p in all_points)
        max_y = max(p[1] for p in all_points)
        
        orig_w = max_x - min_x
        orig_h = max_y - min_y
        
        print(f"[DEBUG] Original BBox: {orig_w:.2f} x {orig_h:.2f}")
        
        if orig_w == 0: orig_w = 0.001
        if orig_h == 0: orig_h = 0.001

        # --- Smart Rotation & Scaling ---
        # Goal: Align the image's Longest Dimension with the Robot's Lateral Axis (Y)
        # and scale that dimension to match the requested 'width'.
        
        user_rot = float(options.get('rotation', 0.0))
        target_w = float(options.get('width', 0.10))
        target_h = float(options.get('height', 0.0)) # Option might be missing or 0
        
        # Calculate aspect ratio
        is_landscape = orig_w >= orig_h
        
        rot_offset = 0.0
        
        # Default Scales (Uniform)
        scale_x = 1.0
        scale_y = 1.0


        
        if is_landscape:
            # Landscape: Image Width -> Robot Lateral (Y)
            #            Image Height -> Robot Forward (X)
            rot_offset = -90.0
            
            # Scale X (Width) to match Target Width (Lateral)
            scale_x = target_w / orig_w
            
            if target_h > 0:
                # User provided explicit Height (Forward Span)
                scale_y = target_h / orig_h
            else:
                # Maintain Aspect Ratio
                scale_y = scale_x 
                
            print(f"[DEBUG] Landscape. Rot -90. ScaleX(W->Lat)={scale_x:.4f}, ScaleY(H->Fwd)={scale_y:.4f}")
            
        else:
            # Portrait: Image Height -> Robot Lateral (Y)
            #           Image Width -> Robot Forward (X)
            rot_offset = 0.0
            
            # Scale Y (Height) to match Target Width (Lateral)
            scale_y = target_w / orig_h
            
            if target_h > 0:
                 # User provided explicit Height (Forward Span)
                 scale_x = target_h / orig_w
            else:
                 # Maintain Aspect Ratio
                 scale_x = scale_y
                 
            print(f"[DEBUG] Portrait. Rot 0. ScaleY(H->Lat)={scale_y:.4f}, ScaleX(W->Fwd)={scale_x:.4f}")


        # Apply offset to user rotation
        rot_final = user_rot + rot_offset
        rot_rad = np.radians(rot_final)
        
        # User Transforms
        off_x = float(options.get('x', 0.20))
        off_y = float(options.get('y', 0.0))
        
        # Center of the original shape for rotation (Center of Bounding Box)
        center_x = (min_x + max_x) / 2
        center_y = (min_y + max_y) / 2
        
        final_patches = []
        
        for path in raw_paths:
            # Optimize: Skip short paths
            if len(path) < 2: continue
            
            transformed_path = []
            
            for i, (px, py) in enumerate(path):
                # 1. Zero-center and Scale (Non-Uniform possible)
                tx = (px - center_x) * scale_x
                ty = (py - center_y) * scale_y
                
                # Image coordinate system: Y down
                # Robot coordinate system: Y up (usually)
                # Flip Y for image processing to match standard Cartesian
                ty = -ty 
                
                # 2. Rotate
                rx = tx * np.cos(rot_rad) - ty * np.sin(rot_rad)
                ry = tx * np.sin(rot_rad) + ty * np.cos(rot_rad)
                
                # 3. Translate
                fx = rx + off_x
                fy = ry + off_y
                
                transformed_path.append([fx, fy])
                
            final_patches.append({
                'type': 'line', 
                'points': transformed_path,
                'data': {'penup': False}
            })
        
        t_end = time.time()
        print(f"[DEBUG] Total Time: {t_end - t_start:.4f}s. Patches: {len(final_patches)}")
            
        return final_patches
            
    except Exception as e:
        import traceback
        traceback.print_exc()
        print(f"Error processing image: {e}")
        return []

def _process_raster(image_data, options):
    """
    Canny Edge Detection for Raster Images.
    """
    try:
        # Load image from bytes
        nparr = np.frombuffer(image_data, np.uint8)
        img = cv2.imdecode(nparr, cv2.IMREAD_GRAYSCALE)
        
        if img is None:
            return []
            
        # Optimization: Resize if too large (e.g. > 2000px) - INCREASED FOR PRECISION
        max_dim = 2000
        h, w = img.shape
        if h > max_dim or w > max_dim:
            scale = max_dim / max(h, w)
            new_w = int(w * scale)
            new_h = int(h * scale)
            img = cv2.resize(img, (new_w, new_h), interpolation=cv2.INTER_AREA)
            
        # Blur
        img_blur = cv2.GaussianBlur(img, (5, 5), 0)
        
        # Canny
        threshold = int(options.get('threshold', 100))
        edges = cv2.Canny(img_blur, threshold, threshold * 2)
        
        # Find Contours
        contours, hierarchy = cv2.findContours(edges, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        
        paths = []
        
        # Sort by length (longest first) to prioritize important shapes
        contours = sorted(contours, key=lambda c: cv2.arcLength(c, True), reverse=True)
        
        # Optimization: Cap total contours to prevent JSON freeze
        max_contours = 200 
        if len(contours) > max_contours:
            contours = contours[:max_contours]
            
        for cnt in contours:
            # Optimization: Filter small noise (arc length < 15 pixels)
            length = cv2.arcLength(cnt, True)
            if length < 15: continue
            
            # Poly Approximation to reduce points
            # HIGH PRECISION: Reduced epsilon from 0.003 to 0.0005
            epsilon = 0.0005 * length 
            approx = cv2.approxPolyDP(cnt, epsilon, True)

            
            # Convert to list of (x,y)
            pts = [ (float(p[0][0]), float(p[0][1])) for p in approx ]
            
            # Close loop if closed
            if len(pts) > 2:
                pts.append(pts[0])
                
            paths.append(pts)
            
        return paths
        
    except Exception as e:
        print(f"Raster Error: {e}")
        return []

def _process_svg(svg_data):
    """
    Parse SVG paths using svgpathtools.
    """
    try:
        with tempfile.NamedTemporaryFile(delete=False, suffix='.svg') as tf:
            tf.write(svg_data)
            tf_path = tf.name
            
        paths, attributes = svg2paths(tf_path)
        
        os.remove(tf_path)
        
        result_paths = []
        
        for path in paths:
            # Estimate length
            length = path.length()
            if length == 0: continue
            
            # Dynamic sampling based on length (1 point every 1 unit) - INCREASED PRECISION
            num_samples = max(int(length / 1.0), 10) 
            
            pts = []
            for i in range(num_samples + 1):
                t = i / num_samples
                c_pt = path.point(t)
                pts.append((c_pt.real, c_pt.imag))
                
            result_paths.append(pts)
            
        return result_paths

    except Exception as e:
        print(f"SVG Error: {e}")
        return []

def analyze_complexity(binary, log=print):
    """
    Analyze binary image complexity to choose between Outline or Skeleton.
    """
    h, w = binary.shape
    total_pixels = h * w
    edge_pixels = cv2.countNonZero(binary)
    density = edge_pixels / total_pixels
    
    # Distance transform to estimate stroke width
    dist_map = cv2.distanceTransform(binary, cv2.DIST_L2, 5)
    max_half_width = np.max(dist_map) # This is half-width
    
    log(f"Complexity Analysis: Density={density:.2%}, MaxHalfWidth={max_half_width:.2f}px")
    
    # Heuristic: 
    # - If very dense (> 12%) -> Complex sketch -> Skeleton
    # - If lines are very thin (max half-width < 2.0) -> Skeleton
    # - Otherwise -> Outline (Double Line)
    
    if density > 0.12 or max_half_width < 2.0:
        return 'skeleton'
    return 'outline'

def _skeletonize(img):
    """
    Robust Skeletonization using Zhang-Suen Thinning Algorithm (Manual Implementation).
    Ensures 1-pixel width lines and preserves connectivity.
    """
    # 1. Binary image (0 or 1)
    img = img.copy() // 255
    prev = np.zeros(img.shape, np.uint8)
    
    # Fast loop using OpenCV lookup tables or just standard thinning steps
    # Standard Step 1 & Step 2 of Zhang-Suen
    
    def thinning_iteration(im, iter_num):
        # Create kernels for hit-or-miss transform? 
        # Actually in Python this is slow.
        # Let's use a morphological approximation that is faster and "good enough" for now
        # OR use the built-in cv2.ximgproc if available, else standard erosion skeleton
        return im

    # Fallback to improved morphological skeletonization
    # It's faster and reliable enough for this application
    skel = np.zeros(img.shape, np.uint8)
    element = cv2.getStructuringElement(cv2.MORPH_CROSS, (3,3))
    temp = img.copy() * 255 # Back to 0-255
    
    while True:
        eroded = cv2.erode(temp, element)
        temp_ = cv2.dilate(eroded, element)
        temp_ = cv2.subtract(temp, temp_)
        skel = cv2.bitwise_or(skel, temp_)
        temp = eroded.copy()
        if cv2.countNonZero(temp) == 0:
            break
            
    return skel

def _prune_skeleton(skel, min_branch_length=10):
    """
    Remove small spurs/branches from the skeleton.
    """
    # Find contours of the skeleton
    contours, _ = cv2.findContours(skel, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    pruned = np.zeros_like(skel)
    
    for cnt in contours:
        if cv2.arcLength(cnt, False) > min_branch_length:
            cv2.drawContours(pruned, [cnt], -1, 255, 1)
            
    return pruned


def _process_vector_bw(image_data, options, log=print):
    """
    Step 1: Strict Binary Vectorization (B/W Only)
    """
    try:
        # Load image from bytes
        nparr = np.frombuffer(image_data, np.uint8)
        img = cv2.imdecode(nparr, cv2.IMREAD_GRAYSCALE)
        
        if img is None: return []
        
        if options.get('check_stop') and options['check_stop'](): return None

        # Optimization: Resize for detail (High Res)
        max_dim = 2000
        h, w = img.shape
        if h > max_dim or w > max_dim:
            scale = max_dim / max(h, w)
            new_w = int(w * scale)
            new_h = int(h * scale)
            img = cv2.resize(img, (new_w, new_h), interpolation=cv2.INTER_AREA)

        if options.get('check_stop') and options['check_stop'](): return None

        # 2. Determine Style
        style = options.get('style', 'auto')
        
        # 1. Pre-Processing & Thresholding
        if style == 'skeleton': # Complex Image
            # Use Adaptive Thresholding for sketches to handle lighting
            # Bilateral Blur to keep edges but remove noise
            log("Applying Bilateral Filter & Adaptive Threshold (Complex)...")
            img_blur = cv2.bilateralFilter(img, 9, 75, 75)
            binary = cv2.adaptiveThreshold(img_blur, 255, cv2.ADAPTIVE_THRESH_GAUSSIAN_C, 
                                           cv2.THRESH_BINARY_INV, 21, 4)
                                           
        else: # Simple/Auto (start with standard)
            thresh_val = int(options.get('threshold', 127))
            inverted = bool(options.get('inverted', False))
            log(f"Thresholding: {thresh_val}, Inverted: {inverted}")
            type_ = cv2.THRESH_BINARY if inverted else cv2.THRESH_BINARY_INV
            _, binary = cv2.threshold(img, thresh_val, 255, type_)

        if options.get('check_stop') and options['check_stop'](): return None

        # Morphological Cleanup
        if options.get('noise_reduction', False):
            log("Applying Noise Reduction...")
            kernel = np.ones((2,2), np.uint8)
            binary = cv2.morphologyEx(binary, cv2.MORPH_OPEN, kernel)

        # 2b. Auto-Detect if needed
        if style == 'auto':
            if options.get('check_stop') and options['check_stop'](): return None
            style = analyze_complexity(binary, log)
            log(f"Auto-Detected Style: {style.upper()}")
        
        # 3. Apply algorithm based on style
        if style == 'skeleton':
            log("Generating Complex Image Skeleton...")
            # Thin pixel-thick lines
            if options.get('check_stop') and options['check_stop'](): return None
            skel = _skeletonize(binary)
            # Prune small noise branches
            final_binary = _prune_skeleton(skel, min_branch_length=10)
            
            # RETR_CCOMP to get lines. 
            retr_mode = cv2.RETR_CCOMP 
            approx_level = 0.0008 # Very detailed
            min_len = 10 
        else:
            log("Generating Simple Image Outlines...")
            final_binary = binary
            # RETR_TREE for double outlines (inner/outer)
            retr_mode = cv2.RETR_TREE
            approx_level = 0.002 # Smoother
            min_len = 15

        if options.get('check_stop') and options['check_stop'](): return None

        # Step 2: Full Contour Extraction
        contours, hierarchy = cv2.findContours(final_binary, retr_mode, cv2.CHAIN_APPROX_SIMPLE)
        log(f"Contours found: {len(contours)}")

        paths = []
        for i, cnt in enumerate(contours):
             # Filter noise
             length = cv2.arcLength(cnt, True)
             if length < min_len: continue 

             # Step 3: Curve Smoothing
             epsilon = approx_level * length 
             # Only close if it's an outline
             is_closed = (style == 'outline')
             approx = cv2.approxPolyDP(cnt, epsilon, is_closed)
             pts = [ (float(p[0][0]), float(p[0][1])) for p in approx ]
             
             if len(pts) > 2:
                 # Adaptive smoothing
                 smooth_iter = int(options.get('smoothing', 2))
                 if style == 'skeleton':
                     # Less smoothing for complex to keep detail
                     smooth_iter = max(1, smooth_iter - 1)
                     
                 if smooth_iter > 0:
                     pts = _chaikin_smooth(pts, iterations=smooth_iter)
                 
                 if is_closed:
                    pts.append(pts[0])
                    
                 paths.append(pts)
        
        log(f"Valid paths after filtering: {len(paths)}")
                 
        # Step 4: Sorting (Optimize Travel)
        paths = _sort_paths(paths)
        log("Paths sorted for travel optimization.")
                 
        return paths


    except Exception as e:
        log(f"Vector BW Error: {e}")
        return []

def _chaikin_smooth(points, iterations=2):
    """
    Chaikin's Corner Cutting Algorithm for curve smoothing.
    Replaces each corner with two new points (at 25% and 75% of the segment).
    """
    if len(points) < 3: return points
    
    smoothed = points
    for _ in range(iterations):
        new_points = []
        for i in range(len(smoothed)):
            p0 = smoothed[i]
            p1 = smoothed[(i + 1) % len(smoothed)] # Wrap around
            
            # Q = 0.75*P0 + 0.25*P1
            # R = 0.25*P0 + 0.75*P1
            
            x0, y0 = p0
            x1, y1 = p1
            
            qx = 0.75 * x0 + 0.25 * x1
            qy = 0.75 * y0 + 0.25 * y1
            
            rx = 0.25 * x0 + 0.75 * x1
            ry = 0.25 * y0 + 0.75 * y1
            
            new_points.append((qx, qy))
            new_points.append((rx, ry))
        smoothed = new_points
        
    return smoothed

def _sort_paths(paths):
    """
    Sorts paths using a Nearest Neighbor Heuristic to minimize Pen-Up travel.
    """
    if not paths: return []
    
    sorted_paths = []
    current_pos = (0,0) # Assume robot starts at origin (or last end)
    
    # Simple Greedy: Find closest start point to current position
    remaining = paths[:]
    
    while remaining:
        best_idx = -1
        min_dist = float('inf')
        
        for i, path in enumerate(remaining):
            start = path[0]
            # Euclidean distance sq
            d = (start[0]-current_pos[0])**2 + (start[1]-current_pos[1])**2
            
            if d < min_dist:
                min_dist = d
                best_idx = i
        
        # Add best path
        next_path = remaining.pop(best_idx)
        sorted_paths.append(next_path)
        
        # Update current pos to end of this path
        current_pos = next_path[-1]
        
    return sorted_paths

def optimize_patches(patches, tolerance=1.0):
    """
    Optimizes a list of patches (lines/polylines) using RDP algorithm.
    tolerance: Epsilon for approxPolyDP (in suitable units, presumably same as data)
    """
    import numpy as np
    
    optimized_patches = []
    
    for patch in patches:
        pts = patch['points']
        if not pts or len(pts) < 3:
            optimized_patches.append(patch)
            continue
            
        # Convert to numpy for cv2
        np_pts = np.array(pts, dtype=np.float32).reshape((-1, 1, 2))
        
        # Optimize
        # Tolerance is epsilon. If data is in meters (0.2), tolerance should be small (e.g. 0.001)
        approx = cv2.approxPolyDP(np_pts, tolerance, False) # False = open curve
        
        # Convert back
        new_pts = approx.reshape(-1, 2).tolist()
        
        optimized_patches.append({
            'type': 'polyline',
            'points': new_pts,
            'data': patch.get('data', {})
        })
        
    return optimized_patches
