#! /usr/bin/env python

# ------------------------------------------------------------------------------
# Imports
# ------------------------------------------------------------------------------

import os
import sys

from pvr import *

import pvr.cameras
import pvr.lights
import pvr.renderers

# ------------------------------------------------------------------------------
# Settings
# ------------------------------------------------------------------------------

reduceRes  =    1
frustumRes =    V3i(2048 / reduceRes, 1556 / reduceRes, 
                    int(200 / pow(reduceRes, 0.5)))
camResMult =    1 / float(reduceRes)
lightResMult =  1 / float(reduceRes)

primType = Prim.Rast.PyroclasticPoint

primParams = {
    # Base controls
    "v"              : V3f(0.0), 
    "radius"         : 0.6,
    "orientation"    : V3f(0, radians(5), radians(5)), 
    "density"        : V3f(5.0),
    "antialiased"    : 1,
    # Noise controls
    "seed"           : 2, 
    "scale"          : 1.5,
    "octaves"        : 8.0,
    "octave_gain"    : 0.5, 
    "lacunarity"     : 1.92,
    "amplitude"      : 1.0,
    "gamma"          : 1.0,
    "pyroclastic_2d" : 0,
    "pyroclastic"    : 0,
    "absolute_noise" : 1
}

raymarcherParams = {
    "use_volume_step_length" : 1,
    "volume_step_length_multiplier" : 0.5, 
    "do_early_termination" : 1,
    "early_termination_threshold" : 0.01
}

RenderGlobals.setupMotionBlur(24.0, 0.5)

# ------------------------------------------------------------------------------
# Camera
# ------------------------------------------------------------------------------

camera = pvr.cameras.standard(1.0 / reduceRes)

# ------------------------------------------------------------------------------
# Modeler
# ------------------------------------------------------------------------------

modeler = Modeler()
modeler.setMapping(Mapping.FrustumMappingType)
modeler.setDataStructure(DataStructure.SparseBufferType)
modeler.setSparseBlockSize(SparseBlockSize.Size16)
modeler.setCamera(camera)

# Add input
input = ModelerInput()
parts = Particles()
parts.add(1)
parts.setPosition(0, V3f(-0.5, 0.0, 0.0))
geo = Geometry()
geo.setParticles(parts)
prim = primType()
prim.setParams(primParams)
input.setGeometry(geo)
input.setVolumePrimitive(prim)

# Process first input
modeler.addInput(input)
modeler.updateBounds()
modeler.setResolution(frustumRes.x, frustumRes.y, frustumRes.z)
modeler.execute()

buffer1 = modeler.buffer()

modeler = Modeler()
modeler.setMapping(Mapping.FrustumMappingType)
modeler.setDataStructure(DataStructure.SparseBufferType)
modeler.setSparseBlockSize(SparseBlockSize.Size16)
modeler.setCamera(camera)

# Add input
primParams["seed"] = 5
input = ModelerInput()
parts = Particles()
parts.add(1)
parts.setPosition(0, V3f(-0.5, 0.0, 0.0))
geo = Geometry()
geo.setParticles(parts)
prim = primType()
prim.setParams(primParams)
input.setGeometry(geo)
input.setVolumePrimitive(prim)

# Process second input
parts.setPosition(0, V3f(0.5, 0.0, 0.0))
modeler.addInput(input)
modeler.updateBounds()
modeler.setResolution(frustumRes.x, frustumRes.y, frustumRes.z)
modeler.execute()

buffer2 = modeler.buffer()

# ------------------------------------------------------------------------------
# Renderer
# ------------------------------------------------------------------------------

renderer = pvr.renderers.standard(raymarcherParams)
renderer.setCamera(camera)

# Volumes

volume1 = VoxelVolume()
volume1.setBuffer(buffer1)
volume1.addAttribute("holdout", V3f(1.5, 2.0, 3.0))

volume2 = VoxelVolume()
volume2.setBuffer(buffer2)
volume2.addAttribute("scattering", V3f(1.5, 2.0, 3.0))

composite = CompositeVolume()
composite.add(volume1)
composite.add(volume2)

renderer.addVolume(composite)

# Lights

occluderType = OtfTransmittanceMapOccluder
lights = pvr.lights.standardThreePoint(renderer, 1.0 / reduceRes,
                                       occluderType)
for light in lights:
    renderer.addLight(light)
renderer.addLight(pvr.lights.standardBehind(renderer, 1.0 / reduceRes,
                                            occluderType))

# Execute render
renderer.printSceneInfo()
renderer.execute()

# Save result
if not os.path.exists("out"):
    os.mkdir("out")
renderer.saveImage("out/image_a.exr")
renderer.saveImage("out/image_a.png")

renderer = pvr.renderers.standard(raymarcherParams)
renderer.setCamera(camera)

# Volumes

volume1 = VoxelVolume()
volume1.setBuffer(buffer1)
volume1.addAttribute("scattering", V3f(1.5, 2.0, 3.0))

volume2 = VoxelVolume()
volume2.setBuffer(buffer2)
volume2.addAttribute("holdout", V3f(1.5, 2.0, 3.0))

composite = CompositeVolume()
composite.add(volume1)
composite.add(volume2)

renderer.addVolume(composite)

# Lights

occluderType = OtfTransmittanceMapOccluder
lights = pvr.lights.standardThreePoint(renderer, 1.0 / reduceRes,
                                       occluderType)
for light in lights:
    renderer.addLight(light)
renderer.addLight(pvr.lights.standardBehind(renderer, 1.0 / reduceRes,
                                            occluderType))

# Execute render
renderer.printSceneInfo()
renderer.execute()

# Save result
renderer.saveImage("out/image_b.exr")
renderer.saveImage("out/image_b.png")

# ------------------------------------------------------------------------------

