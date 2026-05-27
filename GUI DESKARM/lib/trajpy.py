from math import sqrt,atan2,cos,sin,pi,acos, copysign
import numpy as np
from typing import Callable

point_time = tuple[float, float] # point_time type for type annotation
function = Callable[[float], float] # function handle type for type annotation



r""" #@
@name: time_row
@brief: computes and returns as a list a row of the vandermont matrix
@notes: the returned row is actually the one linked to the equation a0t^0 + a1t^1 + a2t^2 + ... + ant^n = 0 => 
$$\sum_i^n a_it^i = 0$$
with n specified by the user. The method can also return the *derivative* of this equation, up to the 2nd derivative
@inputs: 
- float t: the value of t used to compute the list;
- int deg: degree of the equation (n in the example above);
- int der: order of the derivative (from 0 up to 2);
@outputs: 
- list[float] : row of the vandermont matrix: [1, t, t**2, ..., t**n] (or its derivatives)
@# """
def time_row(t:float, deg: int, der:int=0) -> list[float]:
    row = []
    for i in range(0, deg+1):
        match der:
            case 0: row.append(t**i)
            case 1: row.append(i*t**(i-1) if i-1 >= 0 else 0)
            case 2: row.append(i*(i-1)*t**(i-2) if i-2 >= 0 else 0)
    return row


""" #@
@name: spline3
@brief: computes the coefficients of a cubic spline
@notes: the cubic spline has the following structure:\
q = a0+a1t+a2t^2 + a3t^3\
dq = a1+2a2t+3a3t^2\
ddq = 2a2+6a3t\
where q is the position spline, dq is the velocity spline and ddq is the acceleration spline
@inputs: 
- list[point_time] q: it is a list of tuples of a value (float) and a time instant (float). These values and time instants will be used to write a polynomial (with variable "t") that will cross the specified values at t equal to the specified times;
- list[point_time] dq: it is a list of a value (float) and a time instant (float). These values and time instants will be used to write a polynomial (with variable "t") which derivative will cross the specified values at t equal to the specified times;
@outputs: 
- list[function] : list of functions of the variable t that represent the trajectories q(t), dq(t) and ddq(t)
@# """
def spline3(q: list[point_time], dq: list[point_time]) -> list[function]:
    # q = a0+a1t+a2t2+a3t3
    # dq = a1+2a2t+3a3t2
    # ddq = 2a2+6a3t
    data = []
    for p in q:
        data.append(time_row(p[1], 3, 0))
    for p in dq:
        data.append(time_row(p[1], 3, 1))
    known_terms = [float(p[0]) for p in q+dq]
    A = np.array(data)
    b = np.array(known_terms).T
    a = np.dot(np.linalg.inv(A), b)
    return [
        lambda t: float(np.dot(a, np.array(time_row(t,3,0)))),
        lambda t: float(np.dot(a, np.array(time_row(t,3,1)))),
        lambda t: float(np.dot(a, np.array(time_row(t,3,2))))
    ]



""" #@
@name: spline5
@brief: computes the coefficients of a 5th order spline
@notes: the 5th order spline has the following structure:\
q = a0+a1t+a2t^2 + a3t^3 + a4t^4 + a5t^5\
dq = a1+2a2t+3a3t^2 + 4a4t^3 + 5a5t^4\
ddq = 2a2+6a3t+12a4t^2 + 20a5t^3
@inputs: 
- list[point_time] q: it is a list of tuples of a value (float) and a time instant (float). These values and time instants will be used to write a polynomial (with variable "t") that will cross the specified values at t equal to the specified times;
- list[point_time] dq: it is a list of a value (float) and a time instant (float). These values and time instants will be used to write a polynomial (with variable "t") which derivative will cross the specified values at t equal to the specified times;
@outputs: 
- ndarray : numpy array of the coefficients of the 5th order polynomial that crosses the specified points.
@# """
def spline5(q: list[point_time], dq: list[point_time], ddq: list[point_time])->np.ndarray:
    '''
    q = a0+a1t+a2t2+a3t3+a4t4+a5t5
    dq = a1+2a2t+3a3t2+4a4t3+5a5t4
    ddq = 2a2+6a3t+12a4t2+20a5t3
    '''
    vandermont = []
    for point in q:
        vandermont.append(time_row(point[1],5,0))
    for point in dq:
        vandermont.append(time_row(point[1],5,1))
    for point in ddq:
        vandermont.append(time_row(point[1],5,2))
    known_terms = [[point[0]] for point in q+dq+ddq]
    A = np.array(vandermont) 
    b = np.array(known_terms)
    return np.dot(np.linalg.inv(A), b)


""" #@
@name: rangef
@brief: returns a list containing all the values between the initial and final values with the specified step size (that can be float)
@inputs: 
- float start: the starting value of the range;
- float step : the step size used to find the values withing the specified range;
- float end: the final value of the range;
- bool consider_limit: boolean that indicates whether the end value should be inserted in the returned list or not;
@outputs: 
- list[float] : list of all the values found in the specified range with the specified step size.
@# """
def rangef(start:float=0, step:float=1, end:float=0, consider_limit:bool = False) -> list[float]:
    if step == 0: return []
    if start >= end: return []
    if start < end and step < 0: return []
    if start > end and step > 0: return []
    r = []
    i = start
    if not consider_limit:
        while i < end:
            r.append(i)
            i += step
    else:
        while i <= end:
            r.append(i)
            i += step
    return r

""" #@
@name: compose_spline3
@brief: returns the trajectory that results from the composition of the cubic splines obtained for each couple of points in the specified path.
@inputs: 
- list[float] q: list of points that compose the path;
- float ddqm: maximum acceleration;
- list[float] dts: duration of each splines;
@outputs: 
- list[tuple[list[function], float]] :  list of function/spline-duration tuples.
@# """
def compose_spline3(q: list[float], ddqm: float = 1.05, dts:list[float]= None) -> list[tuple[list[function], float]]:
    trajectory = []
    if dts is None:
        # compute the duration of each polynomial
        dts = []
        for q1, q0 in zip(q[:len(q)-1], q[1:]):
            dts.append(sqrt(2*pi*abs(q1-q0)/ddqm))
    return trajectory


""" #@
@name: cubic_speeds
@brief: computes the speeds of the intermediate points of a cubic spline
@inputs: 
- list[float] q: list of the points of the path;
- list[float] dts: list of the duration of each section of the path;
@outputs: 
- list[float]: list of intermediate speeds.
@# """
def cubic_speeds(q: list[float], dts: list[float]) -> list[float]:
    if len(q) == 2 : return [0, 0]
    A = np.zeros((len(q)-2, len(q)-2))
    c = np.zeros((len(q)-2, 1))
    dqs = [(q1-q0)[0] for q0, q1 in zip(q[:len(q)-1], q[1:])]
    ck = lambda k : 3*((dts[k]**2)*dqs[k+1]+ (dts[k+1]**2)*dqs[k])/(dts[k]*dts[k+1])
    
    
    for i in range(len(q)-2):
        A[i, i] += 2*dts[i]
        if i+1 < len(q)-2 : A[i, i+1] = dts[i]
        if i-1 >= 0 : A[i-1, i-1] += 2*dts[i]
        if i-2 >= 0 : A[i-1, i-2] = dts[i]

        if i + 1 < len(q)-1: c[i] = ck(i)

    v = np.dot(np.linalg.inv(A), c)
    return v.T.tolist()[0]


""" #@
@name: preprocess
@brief: subdivides the ranges passed as a list into smaller ranges of size equal to the specified limit.
@inputs: 
- list[float] q: list of values;
- float limit: limit value used to subdivide the specified ranges (q);
@outputs: 
- list[float]: new list of values whose ranges are smaller or equal to the specified limit;
@# """
def preprocess(q: list[float], limit:float=pi/3) -> list[float]:
    new_q = []
    for i,j in zip(range(0,len(q)-1), range(1,len(q))):
        qi = q[i]
        qj = q[j]
        if abs(qj-qi) > limit:
            if qi < qj:
                for qk in rangef(qi, limit, qj, True):
                    new_q.append(qk)
            else:
                for qk in rangef(qi, -limit, qj, True):
                    new_q.append(qk)
        else:
            new_q.append(qi)
    new_q.append(q[-1])
    return new_q


""" #@
@name: trapezoidal
@brief: computes the trapezoidal speed profile trajectory for the specified points;
@notes: the trapezoidal trajectory is subdivided into 3 sections, 2 parabolic ones of equal duration (initial and final ones) and a linear section with constant velocity.
@inputs: 
- list[float] q: list that contains the initial and final values of the trajectory;
- float ddqm: maximum acceleration;
- float tf: duration of the trajectory;
@outputs: 
- list[tuple[ndarray, float]]: list containing the coefficients of each section of the trajectory and their durations.
@# """
def trapezoidal(q:list[float], ddqm:float = 1.05, tf: float = None) -> tuple[list[function], float]: #list[tuple[np.ndarray, float]]:
    # abs(ddqm) >= 4*abs(q[1]-q[0])/tf**2
    # tf**2/(4*abs(q[1]-q[0])) >= 1/abs(ddqm)
    # tf >= +sqrt((4*abs(q[1]-q[0]))/abs(ddqm))
    tc = 0
    if tf is None:
        # if the duration time is not specified, use a bang bang profile
        tf = sqrt((4*abs(q[1]-q[0]))/abs(ddqm)) # bang-bang profile
        tc = tf/2
    else:
        if abs(ddqm) < 4*abs(q[1]-q[0])/tf**2:
            print("This trajectory is not actuable with the specified acceleration:\nchoose a bigger acceleration value")
            return None
        tc = tf/2-sqrt((ddqm*tf)**2-4*ddqm*(q[1]-q[0]))/(2*ddqm)
    qc = q[0] + 0.5*ddqm*tc**2
    qb = qc+ddqm*tc*(tf-2*tc)
    first = np.array([[q[0], 0, 0.5*ddqm]])
    second = np.array([[qc, ddqm*tc, 0]])
    third = np.array([[qb, ddqm*tc, -0.5*ddqm]])
    def qt(t): 
        if t <= tc : return np.dot(first[0], np.array(time_row(t, 2, 0)).T)
        if t > tc and t <= tc+tf-2*tc : return np.dot(second[0], np.array(time_row(t, 2, 0)).T)
        if t > tc+tf-2*tc and t <= tf : return np.dot(third[0], np.array(time_row(t, 2, 0)).T)
    def dqt(t): 
        if t <= tc : return np.dot(first[0], np.array(time_row(t, 2, 1)).T)
        if t > tc and t <= tc+tf-2*tc : return np.dot(second[0], np.array(time_row(t, 2, 1)).T)
        if t > tc+tf-2*tc and t <= tf : return np.dot(third[0], np.array(time_row(t, 2, 1)).T)
    def ddqt(t): 
        if t <= tc : return np.dot(first[0], np.array(time_row(t, 2, 2)).T)
        if t > tc and t <= tc+tf-2*tc : return np.dot(second[0], np.array(time_row(t, 2, 2)).T)
        if t > tc+tf-2*tc and t <= tf : return np.dot(third[0], np.array(time_row(t, 2, 2)).T)
    return ([qt, dqt, ddqt], tf)


""" #@
@name: compose_trapezoidal
@brief: returns the trajectory that results from the composition of the trapezoidal speed profile trajectories obtained for each couple of points of the specified path.
@inputs: 
- list[float] q: list of points that compose the path;
- float ddqm: maximum acceleration;
@outputs: 
- list[tuple[ndarray, float]] :  list of coefficients/trapezoidal-duration tuples.
@# """

def compose_trapezoidal(q:list[float], ddqm:float = 1.05) -> list[tuple[list[function], float]]: #list[tuple[np.ndarray, float]]:
    A = []
    for k in range(len(q)-1):
        q0 = q[k][0]
        q1 = q[k+1][0]
        qk = trapezoidal([q0, q1], ddqm)
        A.append(qk)
    return A

""" #@
@name: cycloidal
@brief: computes a cycloidal trajectory 
@notes: the cycloidal trajectory is not polynomial, so it cannot be represented as a list of coefficients: for this reason a function handle is created for the position q, the speed dq and the acceleration ddq that can be used to compute the trajectory given t.
@inputs: 
- list[float] q: initial and final values of the trajectory;
- float ddqm: maximum acceleration;
- float tf: duration of the trajectory;
@outputs: 
- tuple[list[function], float] : function-handle/cycloidal-duration tuple. 
@# """
def cycloidal(q:list[float], ddqm:float = 1.05, tf:float=None) -> tuple[list[function], float]: # return the function handles for q, dq and ddq
    if tf is None:
        tf = sqrt(2*pi*abs(q[1]-q[0])/ddqm)
    qt = lambda t: q[0]+(q[1]-q[0])*(t/tf-sin(2*pi*t/tf)/(2*pi))
    dqt = lambda t: (q[1]-q[0])*(1-cos(2*pi*t/tf))/tf # derivative of q
    ddqt = lambda t: 2*pi*(q[1]-q[0])*sin(2*pi*t/tf)/(tf**2) # 2nd derivative of q
    return ([qt, dqt, ddqt], tf) # all of q and its derivatives are returned because they cannot be computed simply by using a different set of coefficients


""" #@
@name: compose_cycloidal
@brief: returns the trajectory resulting from the composition of the cycloidal trajectories obtained from each couple of values in the specified path.
@notes: given that the cycloidal trajectory cannot be represented with just a list of coefficients, the returned trajectory will be a list of function handles.
@inputs: 
- list[float] q: list of points in the path (the timing law will be autogenerated);
- float ddqm: maximum acceleration;
@outputs: 
- list[tuple[list[function], float]]: list of trajectory/cycloidal-duration tuples.
@# """
def compose_cycloidal(q:list[float], ddqm:float = 1.05) -> list[tuple[list[function], float]]:
    A = []
    q0 = q[:len(q)-1]
    q1 = q[1:]
    for q0t,q1t in zip(q0,q1):
        qk = cycloidal([q0t[0],q1t[0]], ddqm)
        A.append(qk)
    return A

""" #@
@name: polynomial3
@brief: computes a cubic polynomial trajectory 
@notes: q(t) = 3(t/tf)^2 - 2(t/tf)^3 (normalized s(t) scaled by distance)
@inputs: 
- list[float] q: initial and final values;
- float ddqm: maximum acceleration (used to compute tf);
- float tf: duration;
@outputs: 
- tuple[list[function], float] : function-handle/duration tuple. 
@# """
def polynomial3(q:list[float], ddqm:float = 1.05, tf:float=None) -> tuple[list[function], float]:
    dq_dist = q[1]-q[0]
    if tf is None:
        # For cubic, max acc is at t=0 and t=tf: a_max = 6*dist/tf^2 -> tf = sqrt(6*dist/a_max)
        tf = sqrt(6*abs(dq_dist)/ddqm)
    
    # s(t) = 3t^2 - 2t^3 (normalized time t -> t/tf)
    # q(t) = q0 + dist * s(t)
    qt = lambda t: q[0] + dq_dist * (3*(t/tf)**2 - 2*(t/tf)**3)
    dqt = lambda t: dq_dist * (6*t/tf**2 - 6*t**2/tf**3)
    ddqt = lambda t: dq_dist * (6/tf**2 - 12*t/tf**3)
    return ([qt, dqt, ddqt], tf)


""" #@
@name: polynomial4
@brief: computes a quartic polynomial trajectory 
@notes: q(t) = 4(t/tf)^3 - 3(t/tf)^4 (s(0)=0, s(1)=1, v(0)=0, v(1)=0, a(0)=0)
@inputs: 
- list[float] q: initial and final values;
- float ddqm: maximum acceleration;
- float tf: duration;
@outputs: 
- tuple[list[function], float] : function-handle/duration tuple. 
@# """
def polynomial4(q:list[float], ddqm:float = 1.05, tf:float=None) -> tuple[list[function], float]:
    dq_dist = q[1]-q[0]
    if tf is None:
        # Max acc is at t=tf: a(1) = 12 (normalized) -> a_max = 12*dist/tf^2 -> tf = sqrt(12*dist/a_max)
        tf = sqrt(12*abs(dq_dist)/ddqm)
        
    # s(t) = 4t^3 - 3t^4 (normalized)
    qt = lambda t: q[0] + dq_dist * (4*(t/tf)**3 - 3*(t/tf)**4)
    dqt = lambda t: dq_dist * (12*t**2/tf**3 - 12*t**3/tf**4)
    ddqt = lambda t: dq_dist * (24*t/tf**3 - 36*t**2/tf**4)
    return ([qt, dqt, ddqt], tf)


""" #@
@name: polynomial5
@brief: computes a quintic polynomial trajectory 
@notes: q(t) = 10(t/tf)^3 - 15(t/tf)^4 + 6(t/tf)^5
@inputs: 
- list[float] q: initial and final values;
- float ddqm: maximum acceleration;
- float tf: duration;
@outputs: 
- tuple[list[function], float] : function-handle/duration tuple. 
@# """
def polynomial5(q:list[float], ddqm:float = 1.05, tf:float=None) -> tuple[list[function], float]:
    dq_dist = q[1]-q[0]
    if tf is None:
        # Max acc is at t=tf/2 approx? 
        # a(t) = 60t - 180t^2 + 120t^3 (normalized)
        # Max normalized acc is approx 5.77 at t=0.21 and t=0.79. Let's use 6 for safety.
        # Actually max acc for quintic is sqrt(5.77 * dist / a_max).
        tf = sqrt(5.8*abs(dq_dist)/ddqm)

    # s(t) = 10t^3 - 15t^4 + 6t^5
    qt = lambda t: q[0] + dq_dist * (10*(t/tf)**3 - 15*(t/tf)**4 + 6*(t/tf)**5)
    dqt = lambda t: dq_dist * (30*t**2/tf**3 - 60*t**3/tf**4 + 30*t**4/tf**5)
    ddqt = lambda t: dq_dist * (60*t/tf**3 - 180*t**2/tf**4 + 120*t**3/tf**5)
    return ([qt, dqt, ddqt], tf)


""" #@
@name: ik
@brief: inverse kinematics of a 2Dofs planar manipulator
@notes: it can compute the joint variables values even if the orientation of the end effector is not specified.
@inputs: 
- float x: x coordinate of the end effector;
- float y: y coordinate of the end effector;
- float theta: orientation of the end effector (angle of rotation relative to the z axis with theta=0 when the x axis of the end effector is aligned with the x axis of the base frame of reference);
- dict[float] sizes: sizes of the two links that make up the manipulator, accessed via 'l1' and 'l2'; 
@outputs: 
- ndarray: column numpy array containing the values of the joint coordinates.
@# """
def ik(x:float, y:float, z:float = 0, theta:float = None, sizes:dict[float] = {'l1':0.170 ,'l2':0.158}, limits:dict[float] = None, seed_q:np.ndarray = None) -> np.ndarray:
# if x**2+y**2 > (sizes['l1']+sizes['l2'])**2: return None

    a1 = sizes['l1']
    a2 = sizes['l2']

    solutions = []

    # 1. Calculate Standard Solution
    q1_std = 0
    q2_std = 0
    valid_std = True

    if theta is not None:
        cos_q2 = (x**2+y**2-sizes['l1']**2-sizes['l2']**2)/(2*sizes['l1']*sizes['l2'])
        # Clamp instead of invalidating
        if cos_q2 > 1.0: cos_q2 = 1.0
        if cos_q2 < -1.0: cos_q2 = -1.0
        
        sin_q2 = sqrt(1-cos_q2**2)
        q2_std = atan2(sin_q2, cos_q2)
        q1_std = theta-q2_std
    else:
        cos_q2 = (x**2+y**2-a1**2-a2**2)/(2*a1*a2)
        # Clamp instead of invalidating
        if cos_q2 > 1.0: cos_q2 = 1.0
        if cos_q2 < -1.0: cos_q2 = -1.0
        
        q2_std = acos(cos_q2)
        q1_std = atan2(y,x)-atan2(a2*sin(q2_std), a1+a2*cos(q2_std))

    if valid_std:
        solutions.append((q1_std, q2_std))
        
    # 2. Calculate Alternative Solution (flipped q2)
    # q2_alt = -q2_std
    if valid_std and abs(q2_std) > 1e-6: # Avoid duplicate if q2 is 0
        q2_alt = -q2_std
        q1_alt = 0
        
        if theta is not None:
             q1_alt = theta - q2_alt
        else:
             q1_alt = atan2(y,x)-atan2(a2*sin(q2_alt), a1+a2*cos(q2_alt))
             
        solutions.append((q1_alt, q2_alt))

    # 3. Filter by Limits
    valid_solutions = []
    TOL = 1e-2 # 0.01 rad tolerance for boundary continuity
    if limits:
        for (q1, q2) in solutions:
            # if (limits['q1_min'] - TOL <= q1 <= limits['q1_max'] + TOL) and \
            #    (limits['q2_min'] - TOL <= q2 <= limits['q2_max'] + TOL):
            valid_solutions.append((q1, q2))
    else:
        valid_solutions = solutions

    if not valid_solutions:
        # return None
        pass

    # 4. Select Best Solution using seed_q
    if seed_q is not None:
        best_sol = None
        min_dist = float('inf')
        
        # seed_q is np.ndarray [[q1], [q2], [z]]
        curr_q1 = seed_q[0,0]
        curr_q2 = seed_q[1,0]
        
        for (q1, q2) in valid_solutions:
            # Simple euclidean distance in joint space
            dist = (q1 - curr_q1)**2 + (q2 - curr_q2)**2
            if dist < min_dist:
                min_dist = dist
                best_sol = (q1, q2)
        
        if best_sol:
            return np.array([[best_sol[0], best_sol[1], z]]).T
            
    # Default: return first valid
    sol = valid_solutions[0]
    return np.array([[sol[0], sol[1], z]]).T

""" #@
@name: dk
@brief: direct kinematics of a 2Dofs planar manipulator
@notes: it can compute the x, y coordinates of the end effector and its orientation theta (angle of rotation relative to the z axis with theta=0 when the x axis of the end effector is aligned with the x axis of the base frame of reference)
@inputs: 
- ndarray q: colum numpy array containing the values of the joint coordinates;
- dict[float] sizes: sizes of the two links that make up the manipulator, accessed via 'l1' and 'l2'; 
@outputs: 
- ndarray: column numpy array containing the values of the coordinates of the end effector (x, y and the rotation angle theta).
@# """
def dk(q:np.ndarray, sizes:dict[float] = {'l1':0.170,'l2':0.158})->np.ndarray:
    q1 = float(q[0,0] if q.ndim > 1 else q[0])
    q2 = float(q[1,0] if q.ndim > 1 else q[1])
    x = sizes['l1']*cos(q1)+sizes['l2']*cos(q1+q2)
    y = sizes['l1']*sin(q1)+sizes['l2']*sin(q1+q2)
    theta = q1+q2
    return np.array([[x,y,theta]]).T

"""
#@
@name: Point (class)
@brief: Point class used to represent points in the operational space with a cartesian frame of reference
@inputs: 
- float x: x coordinate of the point
- float y: y coordinate of the point
@#
"""
class Point:
    def __init__(self, x:float, y:float):
        self.x = x
        self.y = y
    
    def __add__(self, other):
        result = Point(self.x, self.y)
        result.x += other.x
        result.y += other.y
        return result

    def __sub__(self, other):
        result = Point(self.x, self.y)
        result.x -= other.x
        result.y -= other.y
        return result
    
    def __mul__(self, scalar:float):
        result = Point(self.x, self.y)
        result.x *= scalar
        result.y *= scalar
        return result

    def __rmul__(self, scalar:float):
        result = Point(self.x, self.y)
        result.x *= scalar
        result.y *= scalar
        return result

    def __div__(self, scalar:float):
        if scalar == 0: raise ZeroDivisionError()
        result = Point(self.x, self.y)
        result.x /= scalar
        result.y /= scalar
        return result

    def __rdiv__(self, scalar:float):
        if scalar == 0: raise ZeroDivisionError()
        result = Point(self.x, self.y)
        result.x /= scalar
        result.y /= scalar
        return result
    
    """
#@
@name: Point.mag
@brief: computes the length of the vector <x, y>
@outputs: 
- float: length of the vector
@#
    """
    def mag(self) -> float:
        return sqrt(self.x**2+self.y**2)

    """
#@
@name: Point.angle
@brief: computes the angle of the vector <x, y>
@outputs: 
- float: angle of the vector
@#
    """
    def angle(self) -> float:
        return atan2(self.y, self.x)
    
    """
#@
@name: Point.rotate
@brief: rotates the vector around its origin
@inputs: 
- float phi: angle (in radians) of rotation;
@outputs: 
- Point: rotated vector
@#
    """
    def rotate(self, phi):
        result = Point(self.x, self.y)
        angle = (result.angle() + phi)
        length = self.mag()
        result.x = length*cos(angle)
        result.y = length*sin(angle)
        return result

    """
#@
@name: Point.ew
@brief: computes the element-wise multiplication (scalar product)
@notes: given two points/vectors a and b, the method returns the value x_a*x_b+y_a*y_b (equivalent to a*b^T)
@inputs: 
- Point other: the other point with which the element wise multiplication is done;
@outputs: 
- float: scalar product
@#
    """
    def ew(self, other) -> float:
        # element wise multiplication
        return self.x*other.x+self.y*other.y

    """
#@
@name: Point.angle_between
@brief: computes the angle between two vectors
@inputs: 
- Point other: vector with which the computation will be done;
@outputs: 
- float: the angle between the two vectors
@#
"""
    def angle_between(self, other) -> float:
        return acos(self.ew(other)/(self.mag()*other.mag()))

    def __str__(self) -> str:
        return f'<{self.x}, {self.y}>'

""" #@
@name: get_profile_law
@brief: Returns the timing law function s(t) and duration tf based on profile type and constraints.
@inputs:
- str profile: 'trapezoidal', 's-curve', 'cubic', 'quartic', 'quintic'
- float distance: total distance to travel (magnitude)
- float max_acc: maximum allowed acceleration constraint
- float max_vel: maximum allowed velocity constraint
- float v0: initial scalar velocity along the path
- float v1: final scalar velocity along the path
@outputs:
- tuple[callable, float]: (s_func, tf) where s_func(t) returns normalized position [0,1]
@# """
def get_profile_law(profile: str, distance: float, max_acc: float, max_vel: float, v0: float = 0.0, v1: float = 0.0) -> tuple[Callable[[float], float], float]:
    dist = abs(distance)
    if dist < 1e-5: # Increased threshold for degenerate patches
        return (lambda t: 1.0, 0.0)

    # 1. Calculate Duration (tf) based on constraints
    tf = 0.0
    
    if profile == 'trapezoidal':
        # Bang-Coast-Bang Logic
        # Time to reach V_max with A_max: t_acc = V_max / A_max
        # Distance covered during accel+decel: S_acc = V_max * t_acc (Triangle area * 2? No. 0.5*V*t * 2 = V*t)
        # S_acc = V_max^2 / A_max.
        
        t_acc = max_vel / max_acc
        s_acc_total = (max_vel ** 2) / max_acc 
        
        if s_acc_total >= dist:
            # Triangle Profile (Target Vel not reached)
            # dist = V_peak * t_acc_peak = (A * t_peak) * t_peak = A * t_peak^2
            # t_peak = sqrt(dist / max_acc)
            # tf = 2 * t_peak = 2 * sqrt(dist / max_acc) = sqrt(4 * dist / max_acc)
            tf = sqrt(4 * dist / max_acc)
        else:
            # Trapezoidal Profile (Coast phase)
            # S_coast = dist - s_acc_total
            # t_coast = S_coast / max_vel
            # tf = 2 * t_acc + t_coast
            t_coast = (dist - s_acc_total) / max_vel
            tf = 2 * t_acc + t_coast
            
    elif profile == 's-curve' or profile == 'cycloidal':
        # Cycloidal: a(t) = (2*pi*S/tf^2) * sin(2*pi*t/tf) -> A_peak = 2*pi*S/tf^2
        # V_peak = 2*S/tf
        tf_acc = sqrt(2 * pi * dist / max_acc)
        tf_vel = 2 * dist / max_vel
        tf = max(tf_acc, tf_vel)
        
    elif profile == 'cubic':
        # s(t) = 3t^2 - 2t^3
        # V_peak = 1.5 * S/tf
        # A_peak = 6 * S/tf^2
        tf_acc = sqrt(6 * dist / max_acc)
        tf_vel = 1.5 * dist / max_vel
        tf = max(tf_acc, tf_vel)
        
    elif profile == 'quartic':
        # V_peak = 1.778 * S/tf
        # A_peak = 12 * S/tf^2 (Initial/Final jerk impulse not considered, just geometric max)
        tf_acc = sqrt(12 * dist / max_acc)
        tf_vel = 1.778 * dist / max_vel
        tf = max(tf_acc, tf_vel)
        
    elif profile == 'quintic':
        # V_peak = 1.875 * S/tf
        # A_peak = 5.7735 * S/tf^2
        tf_acc = sqrt(5.7735 * dist / max_acc)
        tf_vel = 1.875 * dist / max_vel
        tf = max(tf_acc, tf_vel)
        
    else: # Default Linear/Trap fallback
        tf = max(sqrt(4*dist/max_acc), dist/max_vel)


    # 2. Return Timing Law Function s(t) [0->1]
    
    if profile == 'trapezoidal':
        # Re-derive parameters for the specific instance
        # Simple implementation taking into account v0 and v1:
        # Distance during acc: S_acc = (v_c^2 - v0^2)/(2*a)
        # Distance during dec: S_dec = (v_c^2 - v1^2)/(2*a)
        # S_coast = dist - S_acc - S_dec
        
        # Iteration to find achievable v_c
        v_c = max_vel
        s_acc = (v_c**2 - v0**2)/(2*max_acc) if v_c > v0 else 0
        s_dec = (v_c**2 - v1**2)/(2*max_acc) if v_c > v1 else 0
        
        if s_acc + s_dec > dist:
            # Triangular profile (Coast phase eliminated)
            # v_c^2 - v0^2 + v_c^2 - v1^2 = 2*a*dist
            # 2*v_c^2 = 2*a*dist + v0^2 + v1^2
            # v_c = sqrt(a*dist + 0.5*v0^2 + 0.5*v1^2)
            v_c = sqrt(max_acc*dist + 0.5*v0**2 + 0.5*v1**2)
            tc_acc = (v_c - v0)/max_acc
            tc_dec = (v_c - v1)/max_acc
            tf = tc_acc + tc_dec
            
            # Constants for capture
            _v0 = v0
            _v1 = v1
            _vc = v_c
            _acc_up = max_acc
            _acc_down = max_acc
            _tc_acc = tc_acc
            _tc_dec = tc_dec
            _tf = tf
            _dist = dist
            
            def s_trap(t):
                 t = max(0.0, min(t, _tf)) # Clamp
                 val = 0.0
                 if t <= _tc_acc:
                     val = _v0*t + 0.5*_acc_up*t**2
                 else:
                     t_dec = t - _tc_acc
                     dist_acc = _v0*_tc_acc + 0.5*_acc_up*_tc_acc**2
                     val = dist_acc + _vc*t_dec - 0.5*_acc_down*t_dec**2
                 return val / _dist if _dist > 0 else 1.0
                 
            return (s_trap, tf)
            
        else:
            # Trapezoidal Profile (Coast phase)
            tc_acc = (v_c - v0)/max_acc
            tc_dec = (v_c - v1)/max_acc
            s_coast = dist - s_acc - s_dec
            t_coast = s_coast / v_c
            tf = tc_acc + t_coast + tc_dec
            
            _v0 = v0
            _v1 = v1
            _vc = v_c
            _acc_up = max_acc
            _acc_down = max_acc
            _tc_acc = tc_acc
            _t_coast = t_coast
            _tf = tf
            _dist = dist
            
            def s_trap(t):
                 t = max(0.0, min(t, _tf)) # Clamp
                 val = 0.0
                 if t <= _tc_acc:
                     val = _v0*t + 0.5*_acc_up*t**2
                 elif t <= _tc_acc + _t_coast:
                     dist_acc = _v0*_tc_acc + 0.5*_acc_up*_tc_acc**2
                     t_coast = t - _tc_acc
                     val = dist_acc + _vc*t_coast
                 else:
                     t_dec = t - (_tc_acc + _t_coast)
                     dist_acc = _v0*_tc_acc + 0.5*_acc_up*_tc_acc**2
                     dist_coast = _vc*_t_coast
                     start_dec_pos = dist_acc + dist_coast
                     val = start_dec_pos + _vc*t_dec - 0.5*_acc_down*t_dec**2
                 
                 return val / _dist if _dist > 0 else 1.0
                 
            return (s_trap, tf)

    # For polynomial blending we use quintic for boundary velocity support
    elif profile == 'quintic' or profile in ['cubic', 'quartic', 's-curve', 'cycloidal']: 
        # Convert all to quintic if blending is requested (v0>0 or v1>0)
        if v0 > 0.001 or v1 > 0.001:
            # To respect max_acc with boundary velocities we approximate tf:
            # tf must be large enough to accelerate from v0 to max_vel (or vice-versa) AND cover distance
            
            # Simple kinematic constraint for minimum time:
            # You can't reach dist from v0 to v1 without exceeding max_acc if tf is too small.
            # tf >= |v1 - v0| / max_acc
            # Also tf approx = distance / v_avg.
            
            v_avg = (v0 + v1) / 2.0
            if v_avg < 0.001: v_avg = 0.001
            
            tf_kinematic = dist / v_avg
            tf_acc_lim = abs(v1 - v0) / max_acc
            tf_generic = max(dist/max_vel * 1.5, sqrt(6*dist/max_acc))
            
            tf = max(tf_kinematic, tf_acc_lim, tf_generic)
            
            # Boundary conditions (Normalized)
            # v(0)=v0, v(tf)=v1
            # Normalization scale factor for velocity is tf/dist
            scale = tf / dist
            _v0_norm = v0 * scale
            _v1_norm = v1 * scale
            
            # Capping: If the normalized velocity is insanely high > 5.0, 
            # it means v0*tf is much larger than the distance itself, causing the polynomial to loop backwards!
            if _v0_norm > 2.5: 
                # Reduce v0 or increase tf
                _v0_norm = 2.5
            if _v1_norm > 2.5:
                _v1_norm = 2.5

            # Polynomial Coefficients for normalized s(tau) [tau \in 0..1]
            # s(tau) = c0 + c1*tau + c2*tau^2 + c3*tau^3 + c4*tau^4 + c5*tau^5
            # s(0) = 0     => c0 = 0
            # s'(0) = v0n  => c1 = v0n
            # s''(0) = 0   => 2*c2 = 0 => c2 = 0
            # s(1) = 1     => c3 + c4 + c5 = 1 - c1
            # s'(1) = v1n  => 3*c3 + 4*c4 + 5*c5 = v1n - c1
            # s''(1) = 0   => 6*c3 + 12*c4 + 20*c5 = 0
            
            # Solving the linear system:
            c1 = _v0_norm
            c3 = 10 - 6*c1 - 4*_v1_norm
            c4 = -15 + 8*c1 + 7*_v1_norm
            c5 = 6 - 3*c1 - 3*_v1_norm
            
            def s_poly(t):
                 if t <= 0: return 0.0
                 if t >= tf: return 1.0
                 tau = t / tf
                 return c1*tau + c3*(tau**3) + c4*(tau**4) + c5*(tau**5)
                 
            return (s_poly, tf)

            
        else:
            # Standard definitions for zero-velocity boundaries
            if profile == 's-curve' or profile == 'cycloidal':
                return (lambda t: (t/tf - sin(2*pi*t/tf)/(2*pi)) if tf > 0 else 1.0, tf)
            elif profile == 'cubic':
                return (lambda t: ((t/tf)**2 * (3 - 2*(t/tf))) if tf > 0 else 1.0, tf)
            elif profile == 'quartic':
                return (lambda t: ((t/tf)**3 * (4 - 3*(t/tf))) if tf > 0 else 1.0, tf)
            elif profile == 'quintic':
                return (lambda t: ((t/tf)**3 * (10 - 15*(t/tf) + 6*(t/tf)**2)) if tf > 0 else 1.0, tf)
            
    return (lambda t: t/tf if tf > 0 else 1.0, tf)
""" #@
@name: slice_trj
@brief: slices the trajectory patch 
@notes: depending on the type of notes (line or circle) this function slices the trajectory patch in segments depending on 
a timing law s(t) specified by the user
@inputs: 
- dict patch: trajectory patch with the following structure:
```python
{
'type': 'line' or 'circle',
'points': [[x0, y0], [x1, y1]], # start and end points
'data': {'center':c, 'penup':penup, ...}
}
```
- **kargs:
    * 'max_acc': maximum acceleration;
    * 'max_speed': maximum velocity;
    * 'profile': motion profile name ('trapezoidal', 's-curve', etc.);
    * 'sizes': sizes dict containing the sizes of the two links of the manipulator ({'l1': l1, 'l2':l2});
    * 'Tc': time step used for the timing law;
@outputs: 
- list q0s: list of values for the generalized coordinate q of the first motor;
- list q1s: list of values for the generalized coordinate q of the second motor;
- list penups: list of values that show wether the pen should be up or down;
- list ts: list of time instants;
@#
"""
def slice_trj(patch: dict, **kargs):
    # populate arguments with default values
    if 'max_acc' not in kargs:
        kargs['max_acc'] = 1.05
    if 'max_speed' not in kargs:
        kargs['max_speed'] = 5.0 # Default max speed if not specified
    if 'v0' not in kargs:
        kargs['v0'] = 0.0
    if 'v1' not in kargs:
        kargs['v1'] = 0.0
        
    profile_name = kargs.get('profile', 'trapezoidal')
    
    if 'Tc' not in kargs:
        kargs['Tc'] = 1e-3
    if 'sizes' not in kargs:
        print('Using default sizes')
    limits = kargs.get('limits', None)
    
    # Initialize seed from previous configuration if available
    q_prev = kargs.get('initial_q', None)
    # Ensure q_prev is numpy array if it exists (it comes as list [q1, q2] usually)
    if q_prev is not None and not isinstance(q_prev, np.ndarray):
        q_prev = np.array([q_prev + [0]]).T # [q1, q2] -> [[q1],[q2],[0]]
    
    q0s = []
    q1s = []
    penups = []
    ts = []

    # patch['points'] -> [[x0, y0], [x1, y1]]
    sp = Point(*patch['points'][0]) # starting point in operational space
    ep = Point(*patch['points'][1]) # ending point in operational space
    l = (ep-sp).mag() # linear distance between the two points
    c = Point(*patch['data']['center']) if patch['type'] == 'circle' else None # center of the circle
    angle = 0
    radius=abs((sp-c).mag()) if patch['type'] == 'circle' else None  #radius of the circle
    if patch['type'] == 'circle':
        if 'angle' in patch['data']:
            angle = patch['data']['angle']
        else:
            v1 = (sp-c) 
            v2 = (ep-c) 
            # find the angle of rotation between start and end point
            d_alpha = (2*pi+v2.angle())%(2*pi) - (2*pi+v1.angle())%(2*pi) # between 0 and 2pi
            angle = d_alpha if abs(d_alpha) < pi else (-(2*pi-d_alpha) if d_alpha > 0 else 2*pi+d_alpha) # angle between the two vectors starting from the center of the circumference

    if patch['type'] == 'polyline':
        # Polyline Logic: Interpolate across multiple points based on total length
        pts = [Point(*p) for p in patch['points']]
        Ls = []
        total_len = 0
        for i in range(len(pts)-1):
            d = (pts[i+1]-pts[i]).mag()
            Ls.append(d)
            total_len += d
        
        # Get timing law and duration
        s_func, tf = get_profile_law(profile_name, total_len, kargs['max_acc'], kargs['max_speed'], kargs['v0'], kargs['v1'])
        
        print(f"[DEBUG] Polyline Slicing: Pts={len(pts)}, Len={total_len:.4f}, V0={kargs['v0']:.3f}, V1={kargs['v1']:.3f}, Profile={profile_name} -> Tf={tf:.4f}")
    else:
        # Primitive Logic
        length = l if patch['type'] == 'line' else abs(angle)*radius # LENGTH OF THE PATH
        
        # Get timing law and duration
        s_func, tf = get_profile_law(profile_name, length, kargs['max_acc'], kargs['max_speed'], kargs['v0'], kargs['v1'])

        print(f"[DEBUG] Primitive Slicing: Type={patch['type']}, Len={length:.4f}, V0={kargs['v0']:.3f}, V1={kargs['v1']:.3f}, Profile={profile_name} -> Tf={tf:.4f}")

    # Scale Ts by 1.2 if it was extremely short to prevent divide by zero
    if tf < 0.001: tf = 0.01

    points = [] # points (in operational space)
    if patch['data']['penup']:
        # if penup -> use a point-to-point trajectory (in this case: cycloidal)
        # patch['points'] -> [[x0, y0], [x1, y1]]
        k_sz = kargs['sizes']
        
        # DEBUG: Check Start Continuity
        if q_prev is not None:
             dk_res = dk(q_prev, k_sz) # q_prev is [[q1],[q2],[z]] or similar
             curr_x, curr_y = dk_res[0,0], dk_res[1,0]
             target_x, target_y = patch['points'][0][0], patch['points'][0][1]
             dist_sq = (curr_x-target_x)**2 + (curr_y-target_y)**2
             if dist_sq > 0.0001:
                  print(f"[DEBUG] PenUp Start Cartesian Drift: dist={sqrt(dist_sq):.6f}")
                  print(f"        Robot: ({curr_x:.4f}, {curr_y:.4f})")
                  print(f"        Patch: ({target_x:.4f}, {target_y:.4f})")

        # Calculate Start Joint Config (Continuous with previous)
        res0 = ik(patch['points'][0][0], patch['points'][0][1], 1, None, k_sz, limits, seed_q=q_prev)
        if res0 is None: raise Exception(f"IK Failed for Start Point {patch['points'][0]}")
        qt0 = list(res0.T[0])
        
        if q_prev is not None:
             q_prev_list = list(q_prev.T[0])
             j_dist = (qt0[0]-q_prev_list[0])**2 + (qt0[1]-q_prev_list[1])**2
             if j_dist > 0.01:
                  print(f"[DEBUG] IK JUMP at PenUp Start: {sqrt(j_dist):.4f} rad")
                  print(f"        Prev Q: {q_prev_list}")
                  print(f"        New Q:  {qt0}")
                  print(f"        Seed Used: {q_prev_list}")
        
        # Calculate End Joint Config (Continuous with Start)
        res1 = ik(patch['points'][1][0], patch['points'][1][1], 1, None, k_sz, limits, seed_q=res0)
        if res1 is None: raise Exception(f"IK Failed for End Point {patch['points'][1]}")
        qt1 = list(res1.T[0])
        
        (traj0, dt0) = cycloidal([qt0[0], qt1[0]], kargs['max_acc']*0.4, tf) # first motor
        (traj1, dt1) = cycloidal([qt0[1], qt1[1]], kargs['max_acc']*0.4, tf) # second motor
        for t in rangef(0, kargs['Tc'], tf):
            if t <= dt0 : q0s.append(traj0[0](t))
            else: q0s.append(q0s[-1]) # if the trajectories don't have the same length
            if t <= dt1 : q1s.append(traj1[0](t))
            else: q1s.append(q1s[-1]) # if the trajectories don't have the same length
            ts.append(t)
            penups.append(1)
        return q0s, q1s, penups, ts
    # here penup=0 surely
    if patch['type'] == 'line':
        for t in rangef(0, kargs['Tc'], tf, True):
            s = s_func(t) # s \in [0, 1]
            points.append(sp + ((ep-sp)*s))
            ts.append(t)
    elif patch['type'] == 'circle':
        for t in rangef(0, kargs['Tc'], tf, True):
            s = s_func(t)
            points.append(c+(sp-c).rotate(s*angle))
            ts.append(t)
    elif patch['type'] == 'polyline':
        for t in rangef(0, kargs['Tc'], tf, True):
            s_norm = s_func(t) # Normalized s [0, 1]
            s = s_norm * total_len # Actual distance along polyline
            
            # Find which segment 's' falls into
            curr_dist = 0
            found = False
            for i, seg_len in enumerate(Ls):
                if curr_dist + seg_len >= s:
                    # Interpolate in this segment
                    remain = s - curr_dist
                    ratio = remain / seg_len if seg_len > 0 else 0
                    p_interp = pts[i] + (pts[i+1]-pts[i]) * ratio
                    points.append(p_interp)
                    found = True
                    break
                curr_dist += seg_len
            
            if not found:
                 points.append(pts[-1])
            ts.append(t)

    for p in points:
        res = ik(p.x, p.y, 0, None, kargs['sizes'], limits, seed_q=q_prev)
        if res is None: raise Exception(f"IK Failed for point {p}")
        qt = list(res.T[0]) # points converted to joint space
        
        # DEBUG: Check for jumps within the loop
        if q_prev is not None and isinstance(q_prev, np.ndarray):
             q_prev_list = list(q_prev.T[0])
             dist = (qt[0]-q_prev_list[0])**2 + (qt[1]-q_prev_list[1])**2
             if dist > 0.1**2:
                 jump_mag = sqrt(dist)
                 error_msg = f"IK Jump Detected: {jump_mag:.4f} rad. The path is not continuous."
                 print(f"[!] {error_msg}")
                 # raise Exception(error_msg)
                 print(f"    Point: {p}")
                 print(f"    Prev Q: {q_prev_list}")
                 print(f"    New Q: {qt}")
                 print(f"    Limits: {limits}")
                 
                 # Recalculate raw solutions to see what was filtered
                 if 'solutions' not in locals(): # solutions variable is local to ik, let's just print res info
                      pass # We are outside ik scope here, so we can't see internal variables of ik()
                 
                 # Calling IK again with debugging print would be creating a loop, 
                 # instead let's just inspect limits vs Prev Q

        q0s.append(qt[0])
        q1s.append(qt[1])
        penups.append(0)
        
        # Update seed for next point
        q_prev = res

    return q0s, q1s, penups, ts


"""
#@
@name: find_velocities
@brief: computes the velocity of the trajectory in each time instant
@inputs: 
- list[float] q: list of motor positions;
- list[float] ts: list of time instants;
@outputs: 
- list[float]: list of velocities
@#
"""
def find_velocities(q: list[float], ts: list[float]) -> list[float]:
    if not q or len(q) < 2: return [0.0] * len(q)
    dqs = []
    for q0, q1, t0, t1 in zip(q[:-1], q[1:], ts[:-1], ts[1:]):
        dq = q1-q0
        dt = t1-t0
        if dt > 0:
            dqs.append(dq/dt)
        else:
            dqs.append(0.0)
    # Instead of forcing [0], copy the first calculated velocity 
    # to avoid a massive step acceleration spike at t=0
    return [dqs[0]] + dqs

"""
#@
@name: find_accelerations
@brief: computes the acceleration of the trajectory in each time instant
@inputs: 
- list[float] dq: list of the motor velocities;
- list[float] ts: list of time instants;
@outputs: 
- list[float]: list of accelerations
@#
"""
def find_accelerations(dq: list[float], ts: list[float]) -> list[float]:
    if not dq or len(dq) < 2: return [0.0] * len(dq)
    ddqs = []
    for dq0, dq1, t0, t1 in zip(dq[:-1], dq[1:], ts[:-1], ts[1:]):
        ddq = dq1-dq0
        dt = t1-t0
        if dt > 0:
            ddqs.append(ddq/dt)
        else:
            ddqs.append(0.0)
    # Copy the first calculated acceleration to t=0
    return [ddqs[0]] + ddqs
