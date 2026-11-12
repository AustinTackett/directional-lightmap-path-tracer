## Random Math Stuff

Comment's I wrote while writing the path tracer functions, but I moved them out of the fragment shader because it was getting crowded.

```text
    Moeller–Trumbore

                     M                                               T
        [ -D.x   (B.x-A.x)   (C.x-A.x) ] [ t ]   [ O.x - A.x ]    [ T.x ]
        [ -D.y   (B.y-A.y)   (C.y-A.y) ] [ u ] = [ O.y - A.y ] =  [ T.y ]
        [ -D.z   (B.z-A.z)   (C.z-A.z) ] [ v ]   [ O.z - A.z ]    [ T.z ]

              B                Where t is the distance from the ray origin the the intersection occurs
           u / \    D*t        at and u and v are the the distance along edges AB and AC the intersection
            / <-\--------- O   occurs at.
           A — — C              
               v               T is vector from the rays origin to one of the triangles' vertices
```

```text
    Slab Method

    ____max      for i = [x, y, z]
    |     |         ray enter: min(min.i - o.i / d.i, max.i - o.i / d.i)
    |     |         ray exit:  min(min.i - o.i / d.i, max.i - o.i / d.i)
    min___|
```

```text
    Pdf of cosine weighted hemisphere sampling

    Given a direction from a half hemisphere oriented to the normal of the surface 
    output the probability of choosing that direction. 

    Given a sampling function "sample(w)" that increases in regions of high sample density,
    we need to normalize it by finding "?" to have a valid pdf such that:
        -> integral_+Omega[p(w)]dOmega = 1. 
        -> where integral_+Omega[p(w)]dOmega = integral_0_2pi integral_0_pi/2 [p(w)] sin(theta) dphi dtheta
        -> and w := (theta, phi)
    
    Given p(w) = sample(w) * ?, then the integral_+Omega[sample(w) * ?]dOmega = 1.

    Cosine Weighted Sampling: sample density closer to 45deg, sample(w) = cos(w_theta)
    integral_+Omega[cos(theta) * ?]dOmega = 1 -> ? = 1 / pi 
    p(w) = cos(theta) / pi

        n    w   w := direction
        | t /    n := normal       p(theta, phi) = cos(theta) / pi
        |  /     t := theta        
        | /      
      ———————
```

```text
    Converting samples from uniform pdf to samples from the cosine weighted pdf 

    Use inverse cdf of cosine weighted pdf to convert a uniformly sampled theta and phi into a sample 
    from the cosine weighted pdf.

    pdf_cosine(theta, phi) = cos(theta) / pi

    Joint cdf evaulated for some theta_0 and phi_0 value (we don't actually use this)
    (probability some theta and phi is less than theta_0 and phi_0 using the scalar valued function cdf: R^2 -> [0, 1])
    -> cdf_cosine(theta_0, phi_0) = integral_0_(phi_0) integral_0_(theta_0) [cos(theta) / pi] sin(theta) dtheta dphi 
     = cdf_cosine(theta_0, phi_0) = ( phi * sin^2(theta) ) / (2pi)

    Marginal cdf's evaulated for some theta_0 and phi_0 value (cdf_marginal: R -> [0, 1])
    -> cdf_cosine_theta(theta_0) =  integral_0_2pi integral_0_(theta_0) [cos(theta) / pi] sin(theta) dtheta dphi
    = sin^2(theta_0)
    -> cdf_cosine_phi(phi_0) =  integral_0_(phi_0) integral_0_pi/2 [cos(theta) / pi] sin(theta) dtheta dphi
    = phi_0 / 2pi

    Marginal cdf inverses
    -> cdf_cosine_theta^-1(cdf_cosine_theta(theta_0)) = theta_0
    = cdf_cosine_theta^-1( sin^2(theta_0) ) = theta_0
    -> given that arcsin(sqrt(x)) is the inverse of sin^2(x)
    -> cdf_cosine_theta^-1(x) = arcsin(sqrt(x))
    
    -> cdf_cosine_phi^-1(cdf_cosine_phi(phi_0)) = phi_0
    = cdf_cosine_phi^-1(phi_0 / 2pi) = phi_0
    -> cdf_cosine_phi^-1(x) = 2pi * x

    Given a uniformly sampled values (u1, u2) contained in [0, 1] we can convert it to the probability from the pdf by using the inverse cdf
    (since theta and phi samples are indepedent we can use the marginal cdf for each variable)
    -> cdf_cosin_theta^-1(u1) = theta
    = arcsin(sqrt(u1))        = theta
    
    -> cdf_cosin_phi^-1(u2) = phi
    = 2pi * u2              = phi
```

```text
    Get new direction oriented with respect to normal of surface

      (side view)                           (top down)
        N    D      T := tangent                 D
        | t /       t := theta                  /        p := phi
        |  /        N := normal                /  p       
   T____| /         D := direction      T-----N
```

```text
    Hdri's

    Transforming 3D directions to spherically mapped equirectangular uvs

    Cartesion to Spherical (y-up coordinate system)
    r     = sqrt(x^2 + y^2 +z^2)   
    theta = arctan(z / x)
    phi   = arcos(y / sqrt(x^2 + y^2 +z^2))
    
    Bounds of angles in spherical coordinates
    u = [-pi, pi] -> range of 2*pi
    v = [0, pi]   -> range of pi
    
    Normalize by range get values from [-1, 1]
    u = (theta / 2*pi) + 0.5  -> (add 0.5 to u to go from [-0.5, 0.5] to [-1, 1])
    v = phi / pi              -> [-1, 1]
    (The OpenGL standard flips v for texture samplers so v' = 1-v)
```