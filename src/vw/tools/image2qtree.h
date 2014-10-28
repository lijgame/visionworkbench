// __BEGIN_LICENSE__
//  Copyright (c) 2006-2013, United States Government as represented by the
//  Administrator of the National Aeronautics and Space Administration. All
//  rights reserved.
//
//  The NASA Vision Workbench is licensed under the Apache License,
//  Version 2.0 (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
// __END_LICENSE__


#ifndef __VW_TOOLS_IMAGE2QTREE_H__
#define __VW_TOOLS_IMAGE2QTREE_H__

#include <vw/Core/ProgressCallback.h>
#include <vw/Core/Log.h>
#include <vw/Math/BBox.h>
#include <vw/Math/Vector.h>
#include <vw/Image/Transform.h>
#include <vw/Image/MaskViews.h>
#include <vw/FileIO/DiskImageResource.h>
#include <vw/FileIO/DiskImageResourceJPEG.h>
#include <vw/FileIO/DiskImageResourcePNG.h>
#include <vw/FileIO/DiskImageView.h>
#include <vw/Cartography/GeoReference.h>
#include <vw/Cartography/GeoTransform.h>
#include <vw/Mosaic/CelestiaQuadTreeConfig.h>
#include <vw/Mosaic/GigapanQuadTreeConfig.h>  // for GigapanQuadTreeConfig
#include <vw/Mosaic/ImageComposite.h>   // for ImageComposite
#include <vw/Mosaic/KMLQuadTreeConfig.h>  // for KMLQuadTreeConfig
#include <vw/Mosaic/QuadTreeConfig.h>   // for QuadTreeConfig
#include <vw/Mosaic/QuadTreeGenerator.h>  // for QuadTreeGenerator
#include <vw/Mosaic/UniviewQuadTreeConfig.h>  // for UniviewQuadTreeConfig
#include <vw/tools/Common.h>

#include <fstream>

#include <boost/algorithm/string/trim.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/filesystem/path_traits.hpp>
#include <boost/foreach.hpp>
namespace fs = boost::filesystem;


VW_DEFINE_ENUM_PROTO(Channel, 5, (NONE, UINT8, UINT16, INT16, FLOAT));
VW_DEFINE_ENUM_PROTO(Mode, 7, (NONE, KML, TMS, UNIVIEW, GMAP, CELESTIA, GIGAPAN));
VW_DEFINE_ENUM_PROTO(DatumOverride, 5, (NONE, WGS84, LUNAR, MARS, SPHERE))
VW_DEFINE_ENUM_PROTO(Projection, 11, (
                                      DEFAULT,
                                      NONE,
                                      SINUSOIDAL,
                                      MERCATOR,
                                      TRANSVERSE_MERCATOR,
                                      ORTHOGRAPHIC,
                                      STEREOGRAPHIC,
                                      LAMBERT_AZIMUTHAL,
                                      LAMBERT_CONFORMAL_CONIC,
                                      UTM,
                                      PLATE_CARREE))

template <class ImageT, class TransformT>
vw::TransformView<ImageT, TransformT>
transform_only( vw::ImageViewBase<ImageT> const& v,
                TransformT const& transform_func ) {
  return vw::TransformView<ImageT, TransformT>(v.impl(),transform_func);
}

struct Options {

  Options() :
    output_file_type("png"),
    module_name("", true),
    tile_size(256),
    jpeg_quality(0, true),
    png_compression(0, true),
    pixel_scale(1),
    pixel_offset(0),
    aspect_ratio(1),
    global_resolution(0, true),
    nodata(0, true),
    north(0, true), south(0, true),
    east(0, true), west(0, true),
    channel_type(Channel::NONE),
    multiband(false),
    help(false),
    normalize(false),
    terrain(false),
    manual(false),
    global(false)
  {}

  std::vector<std::string> input_files;

  std::string output_file_name;
  vw::tools::Tristate<std::string> output_file_type;
  vw::tools::Tristate<std::string> module_name;
  vw::tools::Tristate<double> nudge_x, nudge_y;
  vw::tools::Tristate<vw::uint32> tile_size;
  vw::tools::Tristate<float>  jpeg_quality;
  vw::tools::Tristate<vw::uint32> png_compression;
  vw::tools::Tristate<float>  pixel_scale, pixel_offset;
  vw::tools::Tristate<vw::int32>  aspect_ratio;
  vw::tools::Tristate<vw::uint32> global_resolution;
  vw::tools::Tristate<float>  nodata;
  vw::tools::Tristate<float>  north, south, east, west;

  Channel channel_type;
  Mode mode;

  bool multiband;
  bool help;
  bool normalize;
  bool terrain;
  bool manual;
  bool global;

  struct {
    vw::uint32 draw_order_offset;
    vw::uint32 max_lod_pixels;
  } kml;

  struct proj_{
    Projection type;
    vw::tools::Tristate<double> lat, lon, scale /*=1*/;
    vw::tools::Tristate<double> p1, p2;
    vw::tools::Tristate<vw::int32> utm_zone;
    proj_() :
      type(Projection::DEFAULT),
      scale(1),
      utm_zone(0, true) {}
  } proj;

  struct datum_ {
    DatumOverride type;
    vw::tools::Tristate<float> sphere_radius;
    datum_() : type(DatumOverride::NONE), sphere_radius(0, true) {}
  } datum;

  void validate() {
    VW_ASSERT(!help, vw::tools::Usage());
    VW_ASSERT(input_files.size() > 0,
              vw::tools::Usage() << "Need at least one input image");

    if (datum.type == DatumOverride::SPHERE)
      VW_ASSERT(datum.sphere_radius.set(),
                vw::tools::Usage() << "Sphere datum override requires a radius");

    if(output_file_name.empty())
      output_file_name = fs::path(input_files[0]).replace_extension().string();

    if (global || north.set() || south.set() || east.set() || west.set()) {
      VW_ASSERT(input_files.size() == 1,
                vw::tools::Usage() << "Cannot override georeference information on multiple images");
      VW_ASSERT(global || (north.set() && south.set() && east.set() && west.set()),
                vw::tools::Usage() << "If you provide one, you must provide all of: --north --south --east --west");
      if (global) {
        north = 90; south = -90; east = 180; west = -180;
      }
      manual = true;
    }

    switch (mode) {
    case Mode::NONE:
      VW_ASSERT(input_files.size() == 1,
                vw::tools::Usage() << "Non-georeferenced images cannot be composed");
      break;
    case Mode::CELESTIA:
    case Mode::UNIVIEW:
      VW_ASSERT(module_name.set(),
                vw::tools::Usage() << "Uniview and Celestia require --module-name");
      break;
    default:
      /* nothing */
      break;
    }

    if (proj.type == Projection::NONE)
      VW_ASSERT(input_files.size() == 1,
                vw::tools::Usage() << "Non-georeferenced images cannot be composed");

    if (jpeg_quality.set())
      vw::DiskImageResourceJPEG::set_default_quality( jpeg_quality );
    if (png_compression.set())
      vw::DiskImageResourcePNG::set_default_compression_level( png_compression );
  }
};

// For image stretching.
static float lo_value = vw::ScalarTypeLimits<float>::highest();
static float hi_value = vw::ScalarTypeLimits<float>::lowest();

vw::int32
compute_resolution(const Mode& p,
                   const vw::cartography::GeoTransform& t,
                   const vw::Vector2& v);

void
get_normalize_vals(boost::shared_ptr<vw::DiskImageResource> file,
                   const Options& opt);

template <class PixelT>
void do_normal_mosaic(const Options& opt, const vw::ProgressCallback *progress) {
  using namespace vw;
  DiskImageView<PixelT> img(opt.input_files[0]);
  mosaic::QuadTreeGenerator quadtree(img, opt.output_file_name);
  quadtree.set_tile_size( opt.tile_size );
  quadtree.set_file_type( opt.output_file_type );

  if ( opt.mode != Mode::NONE ) {
    boost::shared_ptr<mosaic::QuadTreeConfig> config =
      mosaic::QuadTreeConfig::make(opt.mode.string());
    config->configure( quadtree );
  }

  vw_out() << "Generating " << opt.mode.string() << " overlay..." << std::endl;
  quadtree.generate( *progress );
}

vw::cartography::GeoReference
make_input_georef(boost::shared_ptr<vw::DiskImageResource> file,
                  const Options& opt);

std::vector<vw::cartography::GeoReference>
load_image_georeferences( const Options& opt, int& total_resolution );

template <class PixelT>
void do_mosaic(const Options& opt, const vw::ProgressCallback *progress) {
  using namespace vw;
  using namespace vw::cartography;

  typedef typename PixelChannelType<PixelT>::type ChannelT;

  // If we're not outputting any special sort of mosaic (just a regular old
  // quadtree, no georeferencing, no metadata), we use a different
  // function.
  if(opt.mode == Mode::NONE || opt.proj.type == Projection::NONE) {
    do_normal_mosaic<PixelT>(opt, progress);
    return;
  }

  // Read in georeference info and compute total resolution.
  int total_resolution = 1024;
  std::vector<GeoReference> georeferences =
    load_image_georeferences( opt, total_resolution );

  boost::shared_ptr<mosaic::QuadTreeConfig> config =
    mosaic::QuadTreeConfig::make(opt.mode.string());

  // Now that we have the best resolution, we can get our output_georef.
  int xresolution = total_resolution / opt.aspect_ratio, yresolution = total_resolution;

  GeoReference output_georef = config->output_georef(xresolution, yresolution);
  vw_out(VerboseDebugMessage, "tool") << "Output Georef:\n" << output_georef << std::endl;

  // Configure the composite.
  mosaic::ImageComposite<PixelT> composite;

  // Add the transformed image files to the composite.
  for(size_t i=0; i < opt.input_files.size(); i++) {
    const std::string& filename = opt.input_files[i];
    const GeoReference& input_georef = georeferences[i];

    boost::shared_ptr<DiskImageResource> file( DiskImageResource::open(filename) );
    GeoTransform geotx( input_georef, output_georef );

    // Even though the output georeference starts at -180, and input georef may start
    // close to 180, here we don't want to correct for this discrepancy.
    geotx.set_offset(Vector2(0, 0));
    
    ImageViewRef<PixelT> source = DiskImageView<PixelT>( file );

    if ( opt.nodata.set() ) {
      vw_out(VerboseDebugMessage, "tool") << "Using nodata value: "
                                          << opt.nodata.value() << "\n";
      source = mask_to_alpha(create_mask(pixel_cast<typename PixelWithoutAlpha<PixelT>::type >(source),ChannelT(opt.nodata.value())));
    } else if ( file->has_nodata_read() ) {
      vw_out(VerboseDebugMessage, "tool") << "Using nodata value: "
                                          << file->nodata_read() << "\n";
      source = mask_to_alpha(create_mask(pixel_cast<typename PixelWithoutAlpha<PixelT>::type >(source),ChannelT(file->nodata_read())));
    }

    bool global = boost::trim_copy(input_georef.proj4_str())=="+proj=longlat" &&
      fabs(input_georef.lonlat_to_pixel(Vector2(-180,0)).x()) < 1 &&
      fabs(input_georef.lonlat_to_pixel(Vector2(180,0)).x() - source.cols()) < 1 &&
      fabs(input_georef.lonlat_to_pixel(Vector2(0,90)).y()) < 1 &&
      fabs(input_georef.lonlat_to_pixel(Vector2(0,-90)).y() - source.rows()) < 1;

    // Do various modifications to the input image here.
    if( opt.pixel_scale.set() || opt.pixel_offset.set() ) {
      vw_out(VerboseDebugMessage, "tool") << "Apply input scaling: "
                                          << opt.pixel_scale.value() << " offset: "
                                          << opt.pixel_offset.value() << "\n";
      source = channel_cast_rescale<ChannelT>( source * opt.pixel_scale.value() + opt.pixel_offset.value() );
    }

    if( opt.normalize ) {
      vw_out(VerboseDebugMessage, "tool") << "Apply normalizing: ["
                                          << lo_value << ", "
                                          << hi_value << "]\n";
      typedef ChannelRange<ChannelT> range_type;
      source = normalize_retain_alpha(source, lo_value, hi_value,
                                      range_type::min(), range_type::max());
    }

    BBox2i bbox = geotx.forward_bbox( BBox2i(0,0,source.cols(),source.rows()) );
    if ( global ) {
      vw_out() << "\t--> Detected global overlay. Using cylindrical edge extension to hide the seam.\n";
      source = crop( transform( source, geotx, source.cols(), source.rows(), CylindricalEdgeExtension() ), bbox );
    } else {
      if ( norm_2(geotx.reverse(geotx.forward(Vector2()))) >
           0.01*norm_2(Vector2(source.cols(),source.rows())) ) {
        // Check for a fault were the forward bbox is correct, however
        // running a reverse through the geotransform projects 360
        // degrees off. Below seems like the only fix possible, as I
        // believe the problem happens because Proj4's fwd_pj will
        // always clamp to [-180,180].

        // However this fix will break in the event that the projection
        // doesn't loop back on itself. However if the projection did
        // that, I don't believe the test condition for this section
        // would be able to throw. This fix would also break if there
        // was a rotation in the georeference transform. GDAL however
        // doesn't support that.

        // For an example, see WAC global mosiac with tiles past the 180
        BBox2i correction(-geotx.reverse(geotx.forward(Vector2()))[0],0,source.cols(),source.rows());
        source = crop(transform_only( crop( interpolate(source), correction ),
                                      geotx ), bbox );
      } else {
        source = transform( source, geotx, bbox );
      }
    }

    // Images that wrap the date line must be added to the composite
    // on both sides.
    if( bbox.max().x() > total_resolution ) {
      composite.insert( source, bbox.min().x()-total_resolution, bbox.min().y() );
    }
    // Images that are in the 180-360 range *only* go on the other side.
    if( bbox.min().x() < xresolution ) {
      composite.insert( source, bbox.min().x(), bbox.min().y() );
    }
  }

  // This box represents the entire input data set, in pixels, in the output
  // projection space. This should NOT include the extra data used to hide
  // seams and such.
  BBox2i total_bbox = composite.bbox();
  total_bbox.crop(BBox2i(0,0,xresolution,yresolution));

  VW_ASSERT(total_bbox.width() > 0 && total_bbox.height() > 0,
            LogicErr() << "Total bbox is empty. Georeference calculation is probably incorrect.");

  if(opt.mode == Mode::KML) {
    BBox2i bbox = total_bbox;
    // Compute a tighter Google Earth coordinate system aligned bounding box.
    int dim = 2 << (int)(log( (double)(std::max)(bbox.width(),bbox.height()) )/log(2.));
    if( dim > total_resolution ) dim = total_resolution;
    total_bbox = BBox2i( (bbox.min().x()/dim)*dim, (bbox.min().y()/dim)*dim, dim, dim );
    if( ! total_bbox.contains( bbox ) ) {
      if( total_bbox.max().x() == xresolution ) total_bbox.min().x() -= dim;
      else total_bbox.max().x() += dim;
      if( total_bbox.max().y() == yresolution ) total_bbox.min().y() -= dim;
      else total_bbox.max().y() += dim;
    }
  }

  // Prepare the composite.
  if(!opt.multiband)
    composite.set_draft_mode( true );
  composite.prepare( total_bbox, *progress );
  VW_ASSERT(composite.rows() > 0 && composite.cols() > 0,
            LogicErr() << "Composite image is empty. Georeference calculation is probably incorrect.");

  mosaic::QuadTreeGenerator quadtree( composite, opt.output_file_name );

  // This whole bit here is terrible. This functionality should be moved into
  // the Config base class somehow.
  if( opt.mode == Mode::KML ) {
    mosaic::KMLQuadTreeConfig *c2 = dynamic_cast<mosaic::KMLQuadTreeConfig*>(config.get());
    BBox2 ll_bbox( -180.0 + (360.0*total_bbox.min().x())/xresolution,
                   180.0 - (360.0*total_bbox.max().y())/yresolution,
                   (360.0*total_bbox.width())/xresolution,
                   (360.0*total_bbox.height())/yresolution );

    c2->set_longlat_bbox( ll_bbox );
    c2->set_max_lod_pixels( opt.kml.max_lod_pixels );
    c2->set_draw_order_offset( opt.kml.draw_order_offset );
  } else if( opt.mode == Mode::CELESTIA ) {
    mosaic::CelestiaQuadTreeConfig *c2 = dynamic_cast<mosaic::CelestiaQuadTreeConfig*>(config.get());
    c2->set_module(opt.module_name.value());
  } else if( opt.mode == Mode::UNIVIEW ) {
    mosaic::UniviewQuadTreeConfig *c2 = dynamic_cast<mosaic::UniviewQuadTreeConfig*>(config.get());
    c2->set_terrain(opt.terrain);
    c2->set_module(opt.module_name.value());
  } else if ( opt.mode == Mode::GIGAPAN ) {
    mosaic::GigapanQuadTreeConfig *c2 = dynamic_cast<mosaic::GigapanQuadTreeConfig*>(config.get());
    BBox2 ll_bbox( -180.0 + (360.0*total_bbox.min().x())/xresolution,
                   180.0 - (360.0*total_bbox.max().y())/yresolution,
                   (360.0*total_bbox.width())/xresolution,
                   (360.0*total_bbox.height())/yresolution );
    c2->set_longlat_bbox( ll_bbox );
  }

  config->configure(quadtree);

  if (opt.tile_size.set())
    quadtree.set_tile_size(opt.tile_size);
  if (opt.output_file_type.set())
    quadtree.set_file_type(opt.output_file_type);

  // This box represents the input data, shifted such that total_bbox.min() is
  // the origin, and cropped to the size of the output resolution.
  BBox2i data_bbox = composite.bbox();
  data_bbox.crop( BBox2i(0,0,total_bbox.width(),total_bbox.height()));

  quadtree.set_crop_bbox(data_bbox);

  // Generate the composite.
  vw_out() << "Generating " << opt.mode.string() << " overlay..." << std::endl;
  quadtree.generate(*progress);
}

#define PROTOTYPE_ALL_CHANNEL_TYPES( PIXELTYPE )        \
  void do_mosaic_##PIXELTYPE##_uint8(const Options&, const vw::ProgressCallback*); \
  void do_mosaic_##PIXELTYPE##_int16(const Options&, const vw::ProgressCallback*); \
  void do_mosaic_##PIXELTYPE##_uint16(const Options&, const vw::ProgressCallback*); \
  void do_mosaic_##PIXELTYPE##_float32(const Options&, const vw::ProgressCallback*);

PROTOTYPE_ALL_CHANNEL_TYPES(PixelGrayA)
PROTOTYPE_ALL_CHANNEL_TYPES(PixelRGBA)

#undef PROTOTYPE_ALL_CHANNEL_TYPES

#endif//__VW_TOOLS_IMAGE2QTREE_H__
