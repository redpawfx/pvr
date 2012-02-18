//----------------------------------------------------------------------------//

/*! \file Sphere.cpp
  Contains implementations of Inst::Sphere class.
 */

//----------------------------------------------------------------------------//
// Includes
//----------------------------------------------------------------------------//

// Header include

#include "pvr/Primitives/Instantiation/Sphere.h"

// System includes

// Library includes

#include <OpenEXR/ImathRandom.h>

// Project includes

#include "pvr/AttrUtil.h"
#include "pvr/Geometry.h"
#include "pvr/Interrupt.h"
#include "pvr/Log.h"
#include "pvr/Math.h"
#include "pvr/ModelerInput.h"
#include "pvr/ModelingUtils.h"
#include "pvr/RenderGlobals.h"

#include "pvr/Primitives/Rasterization/Point.h"

//----------------------------------------------------------------------------//
// Namespaces
//----------------------------------------------------------------------------//

using namespace std;
using namespace Imath;
using namespace Field3D;

using namespace pvr::Geo;
using namespace pvr::Noise;
using namespace pvr::Sys;
using namespace pvr::Util;

//----------------------------------------------------------------------------//

namespace pvr {
namespace Model {
namespace Prim {
namespace Inst {

//----------------------------------------------------------------------------//
// Sphere
//----------------------------------------------------------------------------//

ModelerInput::Ptr Sphere::execute(const Geo::Geometry::CPtr geo) const
{
  typedef AttrVisitor::const_iterator AttrIter;

  assert(geo != NULL);
  assert(geo->particles() != NULL);

  // Sanity check ---

  if (!geo->particles()) {
    Log::warning("Instantiation primitive has no particles. "
                 "Skipping execution.");
    return ModelerInput::Ptr();
  }

  // Handle batches ---

  if (m_batch.done) {
    return ModelerInput::Ptr();
  }

  // Set up output geometry ---

  size_t                 numPoints = numOutputPoints(geo);
  Geometry::Ptr          outGeo    = Geometry::create();
  Particles::Ptr         particles = Particles::create();
  ModelerInput::Ptr      result    = ModelerInput::create();
  Prim::Rast::Point::Ptr pointPrim = Prim::Rast::Point::create();
  
  outGeo->setParticles(particles);
  result->setGeometry(outGeo);
  result->setVolumePrimitive(pointPrim);

  // Cache attributes ---

  AttrTable &points = particles->pointAttrs();
  AttrRef wsV       = points.addVectorAttr("v", Vector(0.0));
  AttrRef radius    = points.addFloatAttr("radius", 1, vector<float>(1, 1.0));
  AttrRef density   = points.addVectorAttr("density", Vector(1.0));

  // Set up batch information ---

  size_t idx    = 0;
  size_t srcIdx = m_batch.lastSourcePointIdx;

  if (m_batch.first) {
    Log::print("Sphere processing " + str(geo->particles()->size()) +
               " input points");
  } else {
    Log::print("Sphere continuing with batch " + str(m_batch.id));
  }

  // Loop over input points ---

  ProgressReporter progress(2.5f, "  ");
  AttrVisitor visitor(geo->particles()->pointAttrs(), m_params);

  for (AttrIter i = visitor.begin(srcIdx), end = visitor.end(); 
       i != end; ++i, ++srcIdx) {
    // Check if user terminated
    Sys::Interrupt::throwOnAbort();
    // Update attributes
    m_attrs.update(i);
    // Noise offset by seed
    Vector nsOffset;
    nsOffset.x = m_attrs.rng.nextf(-100, 100);
    nsOffset.y = m_attrs.rng.nextf(-100, 100);
    nsOffset.z = m_attrs.rng.nextf(-100, 100);
    // For each instance
    size_t batchSize = 
      std::min(m_attrs.batchSize.value(),
               m_attrs.numPoints.value() - 
               static_cast<int>(m_batch.lastInstanceIdx)) -
      static_cast<int>(particles->size());
    particles->add(batchSize);
    for (size_t i = 0; i < batchSize; ++i, ++idx) {
      // Check if user terminated
      Sys::Interrupt::throwOnAbort();
      // Print progress
      progress.update(static_cast<float>(idx) / particles->size());
      // Randomize local space position
      Vector lsP(0.0);
      if (m_attrs.doFill) {
        lsP = solidSphereRand<V3f>(m_attrs.rng);
      } else {
        lsP = hollowSphereRand<V3f>(m_attrs.rng);
      }
      // Define noise space
      V3f nsP = lsP;
      nsP += nsOffset;
      // Set instance position
      V3f instanceWsP = m_attrs.wsCenter;
      instanceWsP += lsP * m_attrs.radius;
      // Apply displacement noise
      //! \todo Should this multiply by radius instead?
      if (m_attrs.doDispNoise) {
        V3f noise = m_attrs.dispFractal->evalVec(nsP);
        instanceWsP += noise * (m_attrs.dispAmplitude / m_attrs.radius);
      }
      // Set instance density  
      V3f instanceDensity = m_attrs.density;
      // Apply density noise
      if (m_attrs.doDensNoise) {
        float noise = m_attrs.densFractal->eval(nsP);
        instanceDensity *= noise;
      }
      // Set instance attributes
      particles->setPosition(idx, instanceWsP);
      points.setVectorAttr(wsV, idx, Vector(0.0));
      points.setVectorAttr(density, idx, instanceDensity);
      points.setFloatAttr(radius, idx, 0, m_attrs.instanceRadius);
    }
    // See if we finished a primitive
    if (m_batch.lastInstanceIdx + batchSize == 
        static_cast<size_t>(m_attrs.numPoints)) {
      m_batch.lastInstanceIdx = 0;
    } else {
      m_batch.lastInstanceIdx += batchSize;
      break;
    }
  }

  Log::print("  Output: " + str(particles->size()) + " points");

  // Update state
  m_batch.id++;
  m_batch.first = false;
  m_batch.lastSourcePointIdx = srcIdx;
  if (srcIdx == geo->particles()->size()) {
    m_batch.done = true;
  }

  return result;
}

//----------------------------------------------------------------------------//

BBox Sphere::wsBounds(Geo::Geometry::CPtr geometry) const
{
  assert(geometry != NULL);
  assert(geometry->particles() != NULL);

  if (!geometry->particles()) {
    Log::warning("Instantiation primitive has no particles. "
                 "Skipping bounds generation.");
    return BBox();
  }

  BBox wsBBox;
  AttrVisitor visitor(geometry->particles()->pointAttrs(), m_params);

  for (AttrVisitor::const_iterator i = visitor.begin(), end = visitor.end(); 
       i != end; ++i) {
    // Update point attrs
    m_attrs.update(i);
    // Pad to account for radius, motion and displacement fractal
    Vector wsStart = m_attrs.wsCenter.value();
    Vector wsEnd = m_attrs.wsCenter.value() + 
      m_attrs.wsVelocity.value() * RenderGlobals::dt();
    Vector pad = m_attrs.radius.as<Vector>();
    if (m_attrs.doDispNoise) {
      Noise::Fractal::Range range = m_attrs.dispFractal->range();
      double maxDispl = m_attrs.dispAmplitude * 
        std::max(std::abs(range.first), std::abs(range.second));
      pad += Vector(maxDispl);
    }
    wsBBox.extendBy(wsStart + pad);
    wsBBox.extendBy(wsStart - pad);
    wsBBox.extendBy(wsEnd + pad);
    wsBBox.extendBy(wsEnd - pad);
  }

  return wsBBox;
}

//----------------------------------------------------------------------------//

size_t Sphere::numOutputPoints(const Geo::Geometry::CPtr geo) const
{
  size_t count = 0;
  AttrVisitor visitor(geo->particles()->pointAttrs(), m_params);

  for (AttrVisitor::const_iterator i = visitor.begin(), end = visitor.end(); 
       i != end; ++i) {
    m_attrs.update(i);
    count += m_attrs.numPoints.value();
  }
  
  return count;
}

//----------------------------------------------------------------------------//
// Point::AttrState
//----------------------------------------------------------------------------//

void Sphere::AttrState::update
(const Geo::AttrVisitor::const_iterator &i)
{
  i.update(wsCenter);
  i.update(wsVelocity);
  i.update(instanceRadius);
  i.update(radius);
  i.update(density);
  i.update(numPoints);
  i.update(doFill);
  i.update(seed);
  i.update(densScale);
  i.update(densOctaves);
  i.update(densOctaveGain);
  i.update(densLacunarity);
  i.update(dispScale);
  i.update(dispOctaves);
  i.update(dispOctaveGain);
  i.update(dispLacunarity);
  i.update(dispAmplitude);
  i.update(doDensNoise);
  i.update(doDispNoise);
  i.update(batchSize);
  // Set up fractals
  NoiseFunction::CPtr densNoise = NoiseFunction::CPtr(new PerlinNoise);
  densFractal = Fractal::CPtr(new fBm(densNoise, densScale, densOctaves, 
                                      densOctaveGain, densLacunarity));
  NoiseFunction::CPtr dispNoise = NoiseFunction::CPtr(new PerlinNoise);
  dispFractal = Fractal::CPtr(new fBm(dispNoise, dispScale, dispOctaves, 
                                      dispOctaveGain, dispLacunarity));
  // Seed random number generator
  rng.init(seed.value());
}

//----------------------------------------------------------------------------//

} // namespace Inst
} // namespace Prim
} // namespace Model
} // namespace pvr

//----------------------------------------------------------------------------//
