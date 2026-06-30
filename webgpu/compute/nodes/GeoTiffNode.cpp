/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#include "GeoTiffNode.h"

#include <QDebug>
#include <gdal_priv.h>
#include <gdalwarper.h>
#include <ogr_srs_api.h>
#include <mutex>
#include <webgpu/base/raii/TextureWithSampler.h>
#include <webgpu/webgpu.h>

namespace webgpu_compute::nodes {

namespace {

struct FormatInfo {
    WGPUTextureFormat wgpu_format;
    GDALDataType gdal_request_type;
    int bytes_per_channel;
    int out_channels; // 1, 2, or 4 (3-band data is padded to 4)
    bool fill_alpha_255; // GDT_Byte: alpha channel filled with 0xFF
    bool fill_alpha_1f; // float: alpha channel filled with 1.0f
    const char* format_name;
};

FormatInfo detect_format(int n_bands, GDALDataType band_type)
{
    int out_ch = (n_bands == 3) ? 4 : std::min(n_bands, 4);
    bool pad = (n_bands == 3);

    switch (band_type) {
    case GDT_Byte:
        switch (out_ch) {
        case 1: return { WGPUTextureFormat_R8Unorm, GDT_Byte, 1, 1, false, false, "R8Unorm" };
        case 2: return { WGPUTextureFormat_RG8Unorm, GDT_Byte, 1, 2, false, false, "RG8Unorm" };
        default: return { WGPUTextureFormat_RGBA8Unorm, GDT_Byte, 1, 4, pad, false, "RGBA8Unorm" };
        }
    case GDT_UInt16:
        switch (out_ch) {
        case 1: return { WGPUTextureFormat_R16Uint, GDT_UInt16, 2, 1, false, false, "R16Uint" };
        case 2: return { WGPUTextureFormat_RG16Uint, GDT_UInt16, 2, 2, false, false, "RG16Uint" };
        default: return { WGPUTextureFormat_RGBA16Uint, GDT_UInt16, 2, 4, false, false, "RGBA16Uint" };
        }
    case GDT_UInt32:
        switch (out_ch) {
        case 1: return { WGPUTextureFormat_R32Uint, GDT_UInt32, 4, 1, false, false, "R32Uint" };
        case 2: return { WGPUTextureFormat_RG32Uint, GDT_UInt32, 4, 2, false, false, "RG32Uint" };
        default: return { WGPUTextureFormat_RGBA32Uint, GDT_UInt32, 4, 4, false, false, "RGBA32Uint" };
        }
    case GDT_Int16:
        switch (out_ch) {
        case 1: return { WGPUTextureFormat_R16Sint, GDT_Int16, 2, 1, false, false, "R16Sint" };
        case 2: return { WGPUTextureFormat_RG16Sint, GDT_Int16, 2, 2, false, false, "RG16Sint" };
        default: return { WGPUTextureFormat_RGBA16Sint, GDT_Int16, 2, 4, false, false, "RGBA16Sint" };
        }
    case GDT_Int32:
        switch (out_ch) {
        case 1: return { WGPUTextureFormat_R32Sint, GDT_Int32, 4, 1, false, false, "R32Sint" };
        case 2: return { WGPUTextureFormat_RG32Sint, GDT_Int32, 4, 2, false, false, "RG32Sint" };
        default: return { WGPUTextureFormat_RGBA32Sint, GDT_Int32, 4, 4, false, false, "RGBA32Sint" };
        }
    case GDT_Float32:
    case GDT_Float64:
        switch (out_ch) {
        case 1: return { WGPUTextureFormat_R32Float, GDT_Float32, 4, 1, false, false, "R32Float" };
        case 2: return { WGPUTextureFormat_RG32Float, GDT_Float32, 4, 2, false, false, "RG32Float" };
        default: return { WGPUTextureFormat_RGBA32Float, GDT_Float32, 4, 4, false, pad, "RGBA32Float" };
        }
    default:
        return { WGPUTextureFormat_Undefined, GDT_Unknown, 0, 0, false, false, "Unknown" };
    }
}

struct GDALClose_deleter {
    void operator()(GDALDataset* ds) const
    {
        if (ds)
            GDALClose(ds);
    }
};
using GDALDatasetPtr = std::unique_ptr<GDALDataset, GDALClose_deleter>;

} // namespace

GeoTiffNode::GeoTiffNode(webgpu::Context& ctx)
    : GeoTiffNode(ctx, GeoTiffNodeSettings())
{
}

GeoTiffNode::GeoTiffNode(webgpu::Context& ctx, const GeoTiffNodeSettings& settings)
    : Node({ InputSocket(*this, "region", data_type<const radix::geometry::Aabb<3, double>*>()) },
          { OutputSocket(*this, "texture", data_type<const webgpu::raii::TextureWithSampler*>(), [this]() { return m_output_texture.get(); }) })
    , m_ctx(&ctx)
    , m_settings(settings)
{
}

void GeoTiffNode::set_settings(const GeoTiffNodeSettings& settings)
{
    m_settings = settings;
    m_cached_region.reset();
    m_load_info = {};
}

void GeoTiffNode::run_impl()
{
    if (!input_socket("region").is_socket_connected()) {
        fail_run("GeoTiffNode: 'region' input not connected");
        return;
    }

    const auto& region = *std::get<data_type<const radix::geometry::Aabb<3, double>*>()>(input_socket("region").get_connected_data());

    if (m_cached_region.has_value() && *m_cached_region == region && m_output_texture) {
        complete_run();
        return;
    }

    if (m_settings.file_path.empty()) {
        fail_run("GeoTiffNode: file_path is empty");
        return;
    }

    static std::once_flag s_gdal_init;
    std::call_once(s_gdal_init, []() {
#ifdef ALP_PROJ_DATA_DIR
        const char* proj_paths[] = { ALP_PROJ_DATA_DIR, nullptr };
        OSRSetPROJSearchPaths(proj_paths);
#endif
        GDALAllRegister();
    });

    GDALDatasetPtr src(static_cast<GDALDataset*>(GDALOpen(m_settings.file_path.c_str(), GA_ReadOnly)));
    if (!src) {
        fail_run("GeoTiffNode: failed to open: " + m_settings.file_path);
        return;
    }

    // Collect source file metadata for HUD display
    LoadInfo info;
    info.src_width = src->GetRasterXSize();
    info.src_height = src->GetRasterYSize();
    info.src_bands = src->GetRasterCount();
    if (info.src_bands > 0)
        info.src_data_type = GDALGetDataTypeName(src->GetRasterBand(1)->GetRasterDataType());
    if (const OGRSpatialReference* srs = src->GetSpatialRef()) {
        const char* name = srs->GetName();
        const char* auth_name = srs->GetAuthorityName(nullptr);
        const char* auth_code = srs->GetAuthorityCode(nullptr);
        info.src_crs = name ? name : "Unknown";
        if (auth_name && auth_code)
            info.src_crs += std::string(" (") + auth_name + ":" + auth_code + ")";
    }

    GDALDatasetPtr warped(static_cast<GDALDataset*>(
        GDALAutoCreateWarpedVRT(static_cast<GDALDatasetH>(src.get()), nullptr, m_settings.target_crs.c_str(), GRA_Bilinear, 0.125, nullptr)));
    if (!warped) {
        fail_run("GeoTiffNode: failed to create warped VRT to " + m_settings.target_crs);
        return;
    }

    // Geo-transform: world = gt[0..5], pixel col/row from world coords:
    //   col = (x - gt[0]) / gt[1]
    //   row = (y - gt[3]) / gt[5]   (gt[5] < 0 for north-up)
    double gt[6];
    warped->GetGeoTransform(gt);

    const double x_pixel_start = (region.min.x - gt[0]) / gt[1];
    const double y_pixel_start = (region.max.y - gt[3]) / gt[5];
    const double x_pixel_end = (region.max.x - gt[0]) / gt[1];
    const double y_pixel_end = (region.min.y - gt[3]) / gt[5];

    int xOff = static_cast<int>(std::floor(x_pixel_start));
    int yOff = static_cast<int>(std::floor(y_pixel_start));
    int xSrcW = static_cast<int>(std::ceil(x_pixel_end)) - xOff;
    int ySrcH = static_cast<int>(std::ceil(y_pixel_end)) - yOff;

    const int raster_w = warped->GetRasterXSize();
    const int raster_h = warped->GetRasterYSize();

    xOff = std::max(0, xOff);
    yOff = std::max(0, yOff);
    if (xOff + xSrcW > raster_w)
        xSrcW = raster_w - xOff;
    if (yOff + ySrcH > raster_h)
        ySrcH = raster_h - yOff;

    if (xSrcW <= 0 || ySrcH <= 0) {
        fail_run("GeoTiffNode: region is fully outside the GeoTIFF extent");
        return;
    }

    const int n_bands = warped->GetRasterCount();
    if (n_bands == 0) {
        fail_run("GeoTiffNode: no raster bands in dataset");
        return;
    }

    const GDALDataType band_type = warped->GetRasterBand(1)->GetRasterDataType();
    const FormatInfo fmt = detect_format(n_bands, band_type);

    if (fmt.wgpu_format == WGPUTextureFormat_Undefined) {
        fail_run("GeoTiffNode: unsupported band data type");
        return;
    }

    // Scale source window to fit MAX_TEXTURE_RESOLUTION
    uint32_t outW, outH;
    if ((uint32_t)xSrcW <= MAX_TEXTURE_RESOLUTION && (uint32_t)ySrcH <= MAX_TEXTURE_RESOLUTION) {
        outW = (uint32_t)xSrcW;
        outH = (uint32_t)ySrcH;
    } else {
        const double scale = std::min((double)MAX_TEXTURE_RESOLUTION / xSrcW, (double)MAX_TEXTURE_RESOLUTION / ySrcH);
        outW = std::max(1u, (uint32_t)(xSrcW * scale));
        outH = std::max(1u, (uint32_t)(ySrcH * scale));
    }

    const int bytes_per_pixel = fmt.bytes_per_channel * fmt.out_channels;
    const size_t total_bytes = (size_t)outW * outH * bytes_per_pixel;

    // Allocate zero-initialized output buffer (zero = transparent/null for un-read channels)
    std::vector<uint8_t> pixel_buf(total_bytes, 0);

    // Build band map (read up to min(n_bands, out_channels) actual bands)
    const int bands_to_read = std::min(n_bands, fmt.out_channels);
    std::vector<int> band_map(bands_to_read);
    for (int i = 0; i < bands_to_read; ++i)
        band_map[i] = i + 1;

    // Pixel-interleaved read: nPixelSpace = bytes_per_pixel, nBandSpace = bytes_per_channel
    const CPLErr err = warped->RasterIO(GF_Read, xOff, yOff, xSrcW, ySrcH, pixel_buf.data(), (int)outW, (int)outH,
        fmt.gdal_request_type, bands_to_read, band_map.data(),
        (GSpacing)bytes_per_pixel, (GSpacing)bytes_per_pixel * outW, (GSpacing)fmt.bytes_per_channel, nullptr);

    if (err != CE_None) {
        fail_run("GeoTiffNode: RasterIO failed with error " + std::to_string(err));
        return;
    }

    // Fill the padded 4th channel for 3-band inputs
    if (fmt.fill_alpha_255) {
        for (size_t i = 0; i < (size_t)outW * outH; ++i)
            pixel_buf[i * bytes_per_pixel + 3] = 0xFF;
    } else if (fmt.fill_alpha_1f) {
        const float one = 1.0f;
        for (size_t i = 0; i < (size_t)outW * outH; ++i)
            std::memcpy(pixel_buf.data() + i * bytes_per_pixel + 3 * fmt.bytes_per_channel, &one, sizeof(float));
    }

    // Create GPU texture
    WGPUTextureDescriptor tex_desc {};
    tex_desc.label = WGPUStringView { .data = "GeoTiffNode output texture", .length = WGPU_STRLEN };
    tex_desc.dimension = WGPUTextureDimension_2D;
    tex_desc.size = { outW, outH, 1 };
    tex_desc.mipLevelCount = 1;
    tex_desc.sampleCount = 1;
    tex_desc.format = fmt.wgpu_format;
    tex_desc.usage = (WGPUTextureUsage)(WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc);

    WGPUSamplerDescriptor smp_desc {};
    smp_desc.label = WGPUStringView { .data = "GeoTiffNode sampler", .length = WGPU_STRLEN };
    smp_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    smp_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    smp_desc.addressModeW = WGPUAddressMode_ClampToEdge;
    smp_desc.magFilter = WGPUFilterMode_Linear;
    smp_desc.minFilter = WGPUFilterMode_Linear;
    smp_desc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    smp_desc.lodMinClamp = 0.0f;
    smp_desc.lodMaxClamp = 1.0f;
    smp_desc.compare = WGPUCompareFunction_Undefined;
    smp_desc.maxAnisotropy = 1;

    m_output_texture = std::make_unique<webgpu::raii::TextureWithSampler>(m_ctx->device(), tex_desc, smp_desc);

    // Upload pixel data
    WGPUTexelCopyTextureInfo dst {};
    dst.texture = m_output_texture->texture().handle();
    dst.mipLevel = 0;
    dst.origin = { 0, 0, 0 };
    dst.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout {};
    layout.offset = 0;
    layout.bytesPerRow = outW * (uint32_t)bytes_per_pixel;
    layout.rowsPerImage = outH;

    const WGPUExtent3D extent { outW, outH, 1 };
    wgpuQueueWriteTexture(m_ctx->queue(), &dst, pixel_buf.data(), total_bytes, &layout, &extent);

    qDebug() << "GeoTiffNode: loaded" << outW << "x" << outH << "texture from" << QString::fromStdString(m_settings.file_path);

    info.out_width = outW;
    info.out_height = outH;
    info.out_format = fmt.format_name;
    info.loaded = true;
    m_load_info = info;
    m_cached_region = region;
    complete_run();
}

void GeoTiffNode::serialize_settings(QJsonObject& out) const
{
    out["file_path"] = QString::fromStdString(m_settings.file_path);
    out["target_crs"] = QString::fromStdString(m_settings.target_crs);
}

void GeoTiffNode::deserialize_settings(const QJsonObject& in)
{
    GeoTiffNodeSettings s = m_settings;
    if (in.contains("file_path"))
        s.file_path = in["file_path"].toString().toStdString();
    if (in.contains("target_crs"))
        s.target_crs = in["target_crs"].toString().toStdString();
    set_settings(s);
}

} // namespace webgpu_compute::nodes
